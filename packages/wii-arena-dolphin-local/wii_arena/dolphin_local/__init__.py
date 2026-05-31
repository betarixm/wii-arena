from __future__ import annotations

import json
import mmap
import os
import shutil
import socket
import subprocess
import tempfile
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator, cast

from wii_arena.dlpack import Driver
from wii_arena.dolphin import (
    Dolphin,
    DolphinFrameBuffer,
    DolphinMemoryView,
    receive_frame_buffer,
)


class LocalDolphin(Dolphin):
    class Session(Dolphin.Session):
        def __init__(
            self,
            memory_view: DolphinMemoryView,
            frame_socket: socket.socket,
            control_socket: socket.socket,
            driver: Driver,
        ):
            self._memory_view = memory_view
            self._frame_socket = frame_socket
            self._control_socket = control_socket
            self._driver = driver

        def memory_view(self) -> DolphinMemoryView:
            return self._memory_view

        def frame_buffer(self) -> DolphinFrameBuffer:
            return self._replace_frame_buffer(
                receive_frame_buffer(self._frame_socket, self._driver)
            )

        @property
        def control_socket(self) -> socket.socket:
            return self._control_socket

    def __init__(
        self,
        executable_path: Path,
        vulcan_layer_path: Path,
        vulkan_layer_configuration_path: Path,
        wii_iso_file: Path,
        display: str,
        driver: Driver,
    ):
        if not display:
            raise ValueError("LocalDolphin requires a non-empty X11 display.")
        self._executable_path = executable_path
        self._wii_iso_file = wii_iso_file
        self._display = display
        self._driver = driver
        self._vulkan_layer_path = vulcan_layer_path
        self._vulkan_layer_configuration_path = vulkan_layer_configuration_path

    @contextmanager
    def session(self) -> Iterator[LocalDolphin.Session]:
        with tempfile.TemporaryDirectory() as socket_directory:
            socket_directory_path = Path(socket_directory)
            frame_socket_path = socket_directory_path / "frame_capture.sock"
            control_socket_path = socket_directory_path / "dolphin_control.sock"
            with _use_running_dolphin_process(
                executable_path=self._executable_path,
                wii_iso_file=self._wii_iso_file,
                vulkan_layer_path=self._vulkan_layer_path,
                vulkan_layer_configuration_path=self._vulkan_layer_configuration_path,
                frame_socket_path=frame_socket_path,
                control_socket_path=control_socket_path,
                display=self._display,
            ) as dolphin_process:
                frame_socket: socket.socket | None = None
                control_socket: socket.socket | None = None
                session: LocalDolphin.Session | None = None
                try:
                    frame_socket = _connect_socket(
                        frame_socket_path, dolphin_process=dolphin_process
                    )
                    control_socket = _connect_socket(
                        control_socket_path, dolphin_process=dolphin_process
                    )
                    memory_view = _resolve_memory_view(
                        dolphin_pid=dolphin_process.pid,
                        dolphin_process=dolphin_process,
                    )
                    session = LocalDolphin.Session(
                        memory_view=memory_view,
                        frame_socket=frame_socket,
                        control_socket=control_socket,
                        driver=self._driver,
                    )
                    yield session
                finally:
                    if session is not None:
                        session.close_frame_buffer()
                    if frame_socket is not None:
                        try:
                            frame_socket.sendall(b"Q")
                        except OSError:
                            pass
                        frame_socket.close()
                    if control_socket is not None:
                        try:
                            control_socket.sendall(b"Q")
                        except OSError:
                            pass
                        control_socket.close()


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


def _connect_socket(
    socket_path: Path,
    timeout_sec: float = 30.0,
    dolphin_process: subprocess.Popen[bytes] | None = None,
) -> socket.socket:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if dolphin_process is not None:
            _ensure_running(dolphin_process)
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
    control_socket_path: Path,
    display: str,
) -> Iterator[subprocess.Popen[bytes]]:
    with tempfile.TemporaryDirectory(prefix="wii-arena-vk-layer-") as temp_directory:
        configuration_path = Path(__file__).parents[3] / "configuration"
        script_path = configuration_path / "controller.py"
        dolphin_settings_path = configuration_path / "settings"
        target_settings_path = Path(temp_directory) / "dolphin-user"
        shutil.copytree(dolphin_settings_path, target_settings_path)
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
        env["CONTROL_SOCKET"] = str(control_socket_path)
        env["DISPLAY"] = display
        dolphin_process = subprocess.Popen(
            [
                str(executable_path),
                f"--exec={wii_iso_file}",
                "--platform=x11",
                "--video_backend=Vulkan",
                f"--user={target_settings_path}",
                f"--script={script_path}",
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
