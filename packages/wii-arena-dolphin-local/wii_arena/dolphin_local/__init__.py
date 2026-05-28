from __future__ import annotations

import array
import json
import logging
import mmap
import os
import socket
import struct
import subprocess
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, cast

from wii_arena.dlpack import Driver
from wii_arena.dolphin import (
    Dolphin,
    DolphinAction,
    DolphinFrameBuffer,
    DolphinMemoryView,
)

_LOGGER = logging.getLogger(__name__)


class LocalDolphin(Dolphin):
    class Session(Dolphin.Session):
        def __init__(
            self,
            memory_view: DolphinMemoryView,
            frame_socket: socket.socket,
            driver: Driver,
        ):
            self._memory_view = memory_view
            self._frame_socket = frame_socket
            self._driver = driver

        def execute(self, action: DolphinAction) -> None:
            _LOGGER.debug("execute called with action=%s (noop)", action)
            return None

        def memory_view(self) -> DolphinMemoryView:
            return self._memory_view

        @contextmanager
        def frame_buffer(self) -> Iterator[DolphinFrameBuffer]:
            self._frame_socket.sendall(b"R")
            fds = array.array("i")
            msg, ancdata, _, _ = self._frame_socket.recvmsg(
                4096, socket.CMSG_LEN(struct.calcsize("i"))
            )
            if not msg:
                raise RuntimeError("Frame socket closed before sending a frame packet.")

            fd: int | None = None
            for cmsg_level, cmsg_type, cmsg_data in ancdata:
                if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
                    fds.frombytes(cmsg_data[: struct.calcsize("i")])
                    if fds:
                        fd = int(fds[0])
                        break
            if fd is None:
                raise RuntimeError("Layer response did not include a frame fd.")

            if len(msg) < 20:
                os.close(fd)
                raise RuntimeError(f"Frame header too short: {len(msg)}")

            _, height, stride, size, _frame_id = struct.unpack("IIIII", msg[:20])
            with self._driver.dlpack_from_file_descriptor(
                fd, size, height, stride
            ) as frame:
                yield cast(DolphinFrameBuffer, frame)

    def __init__(
        self,
        executable_path: Path,
        vulcan_layer_path: Path,
        vulkan_layer_configuration_path: Path,
        wii_iso_file: Path,
        driver: Driver,
    ):
        self._executable_path = executable_path
        self._wii_iso_file = wii_iso_file
        self._driver = driver
        self._vulkan_layer_path = vulcan_layer_path
        self._vulkan_layer_configuration_path = vulkan_layer_configuration_path

    @contextmanager
    def session(self) -> Iterator[LocalDolphin.Session]:
        with tempfile.TemporaryDirectory() as socket_directory:
            socket_directory_path = Path(socket_directory)
            frame_socket_path = socket_directory_path / "frame_capture.sock"
            with _use_running_dolphin_process(
                executable_path=self._executable_path,
                wii_iso_file=self._wii_iso_file,
                vulkan_layer_path=self._vulkan_layer_path,
                vulkan_layer_configuration_path=self._vulkan_layer_configuration_path,
                frame_socket_path=frame_socket_path,
            ) as dolphin_process:
                frame_socket: socket.socket | None = None
                try:
                    frame_socket = _connect_frame_socket(frame_socket_path)
                    memory_view = _resolve_memory_view(
                        dolphin_pid=dolphin_process.pid,
                        dolphin_process=dolphin_process,
                    )
                    yield LocalDolphin.Session(
                        memory_view=memory_view,
                        frame_socket=frame_socket,
                        driver=self._driver,
                    )
                finally:
                    if frame_socket is not None:
                        try:
                            frame_socket.sendall(b"Q")
                        except OSError:
                            pass
                        frame_socket.close()


def _resolve_memory_view(
    dolphin_pid: int,
    dolphin_process: subprocess.Popen[bytes],
    timeout_sec: float = 30.0,
) -> DolphinMemoryView:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        _ensure_running(dolphin_process)
        with _use_dolphin_memory_fd(dolphin_pid=dolphin_pid) as memory_fd:
            if memory_fd is not None:
                mapped = mmap.mmap(
                    memory_fd, 0, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ
                )
                return DolphinMemoryView(memoryview(mapped))
        time.sleep(0.1)
    raise TimeoutError(
        f"Timed out waiting for Dolphin memory view from process {dolphin_pid}"
    )


def _connect_frame_socket(
    socket_path: Path, timeout_sec: float = 30.0
) -> socket.socket:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if socket_path.exists():
            candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            if candidate.connect_ex(str(socket_path)) == 0:
                return candidate
            candidate.close()
        time.sleep(0.1)
    raise TimeoutError(f"Timed out waiting for frame socket at {socket_path}")


@contextmanager
def _use_dolphin_memory_fd(
    dolphin_pid: int,
) -> Iterator[int | None]:
    proc_fd_path = Path(f"/proc/{dolphin_pid}/fd")
    target_fd: int | None = None
    try:
        fd_names = os.listdir(proc_fd_path)
    except OSError:
        yield None
        return
    for fd_name in fd_names:
        path = proc_fd_path / fd_name
        try:
            target = os.readlink(path)
        except OSError:
            continue
        if "dolphin-emu." in target:
            target_fd = int(fd_name)
            break

    if target_fd is None:
        yield None
        return

    memory_fd = os.open(f"/proc/{dolphin_pid}/fd/{target_fd}", os.O_RDONLY)
    try:
        yield memory_fd
    finally:
        os.close(memory_fd)


def _ensure_running(dolphin_process: subprocess.Popen[bytes]) -> None:
    if dolphin_process.poll() is None:
        return

    stdout, stderr = dolphin_process.communicate(timeout=0.1)
    stdout_text = stdout.decode("utf-8", errors="replace").strip()
    stderr_text = stderr.decode("utf-8", errors="replace").strip()
    raise RuntimeError(
        "Local Dolphin process exited early "
        f"(returncode={dolphin_process.returncode}).\n"
        f"stdout:\n{stdout_text}\n\nstderr:\n{stderr_text}"
    )


def _stop_process(dolphin_process: subprocess.Popen[bytes]) -> None:
    if dolphin_process.poll() is not None:
        return
    dolphin_process.terminate()
    try:
        dolphin_process.wait(timeout=3.0)
    except subprocess.TimeoutExpired:
        dolphin_process.kill()
        dolphin_process.wait(timeout=3.0)


@contextmanager
def _use_running_dolphin_process(
    *,
    executable_path: Path,
    wii_iso_file: Path,
    vulkan_layer_path: Path,
    vulkan_layer_configuration_path: Path,
    frame_socket_path: Path,
) -> Iterator[subprocess.Popen[bytes]]:
    with tempfile.TemporaryDirectory(prefix="wii-arena-vk-layer-") as temp_directory:
        env = os.environ.copy()
        _prepend_env_path(env, "LD_LIBRARY_PATH", executable_path.parent.parent / "lib")
        _prepend_env_path(env, "LD_LIBRARY_PATH", vulkan_layer_path.parent)
        layer_name, runtime_layer_config_path = _prepare_runtime_vulkan_layer_config(
            vulkan_layer_path=vulkan_layer_path,
            vulkan_layer_configuration_path=vulkan_layer_configuration_path,
            runtime_directory=Path(temp_directory),
        )
        env["VK_LAYER_PATH"] = str(runtime_layer_config_path.parent)
        env["VK_INSTANCE_LAYERS"] = layer_name
        env["FRAME_CAPTURE_SOCKET"] = str(frame_socket_path)
        dolphin_process = subprocess.Popen(
            [
                str(executable_path),
                f"--exec={wii_iso_file}",
                "--platform=headless",
                "--video_backend=Vulkan",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        try:
            _ensure_running(dolphin_process)
            yield dolphin_process
        finally:
            _stop_process(dolphin_process)


def _prepend_env_path(env: dict[str, str], name: str, value: Path) -> None:
    current = env.get(name, "")
    if current:
        env[name] = f"{value}:{current}"
    else:
        env[name] = str(value)


def _prepare_runtime_vulkan_layer_config(
    *,
    vulkan_layer_path: Path,
    vulkan_layer_configuration_path: Path,
    runtime_directory: Path,
) -> tuple[str, Path]:
    with vulkan_layer_configuration_path.open("r", encoding="utf-8") as file:
        vulkan_layer_configuration = json.load(file)
    layer = vulkan_layer_configuration.get("layer")
    if not isinstance(layer, dict):
        raise ValueError("Invalid Vulkan layer configuration: missing 'layer' object")
    layer = cast(dict[str, Any], layer)
    layer_name = layer.get("name")
    if not isinstance(layer_name, str) or not layer_name:
        raise ValueError("Invalid Vulkan layer configuration: missing layer name")
    layer["library_path"] = str(vulkan_layer_path)

    runtime_config_path = runtime_directory / vulkan_layer_configuration_path.name
    with runtime_config_path.open("w", encoding="utf-8") as file:
        json.dump(vulkan_layer_configuration, file)
    return layer_name, runtime_config_path
