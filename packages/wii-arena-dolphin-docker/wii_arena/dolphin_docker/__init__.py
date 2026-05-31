from __future__ import annotations

import array
import logging
import mmap
import os
import socket
import struct
import tempfile
import time
from abc import ABC, abstractmethod
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator, cast

from docker import DockerClient
from docker.models.containers import Container
from docker.models.images import Image
from wii_arena.dlpack import Driver
from wii_arena.dolphin import (
    Dolphin,
    DolphinFrameBuffer,
    DolphinMemoryView,
)

_LOGGER = logging.getLogger(__name__)

_DOLPHIN_MEMORY_FD_SCAN_SCRIPT = (
    "import array, os, socket, sys\n"
    "sock_path = sys.argv[1]\n"
    "target_fd = None\n"
    "for fd_name in os.listdir('/proc/1/fd'):\n"
    "    path = f'/proc/1/fd/{fd_name}'\n"
    "    try:\n"
    "        target = os.readlink(path)\n"
    "    except OSError:\n"
    "        continue\n"
    "    if 'dolphin-emu.' in target:\n"
    "        target_fd = int(fd_name)\n"
    "        break\n"
    "if target_fd is None:\n"
    "    raise SystemExit(11)\n"
    "dup_fd = os.open(f'/proc/1/fd/{target_fd}', os.O_RDONLY)\n"
    "sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
    "try:\n"
    "    sock.connect(sock_path)\n"
    "    payload = array.array('i', [dup_fd])\n"
    "    sock.sendmsg([b'M'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, payload)])\n"
    "finally:\n"
    "    sock.close()\n"
    "    os.close(dup_fd)\n"
)


class DockerDolphin(Dolphin, ABC):
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

        @property
        def control_socket(self) -> socket.socket:
            return self._control_socket

    def __init__(
        self,
        docker_image: Image,
        wii_iso_file: Path,
        driver: Driver,
        docker_client: DockerClient | None = None,
        container_socket_directory: str = "/tmp",
    ):
        self._docker_client = (
            docker_client if docker_client is not None else DockerClient.from_env()
        )
        self._docker_image = docker_image
        self._wii_iso_file = wii_iso_file
        self._container_socket_directory = container_socket_directory
        self._driver = driver

    @property
    def _container_frame_socket_file(self) -> str:
        return f"{self._container_socket_directory}/frame_capture.sock"

    @property
    def _container_control_socket_file(self) -> str:
        return f"{self._container_socket_directory}/dolphin_control.sock"

    @contextmanager
    def session(self) -> Iterator[DockerDolphin.Session]:
        with (
            tempfile.TemporaryDirectory() as socket_directory,
            tempfile.TemporaryDirectory() as dev_shm_directory,
        ):
            container = self._container(
                socket_directory=Path(socket_directory),
                dev_shm_directory=Path(dev_shm_directory),
            )
            frame_socket: socket.socket | None = None
            control_socket: socket.socket | None = None
            memory_fd_socket_host = Path(socket_directory) / "memory_fd.sock"
            try:
                container.reload()
                if container.status != "running":
                    logs = container.logs(tail=120).decode("utf-8", errors="replace")
                    raise RuntimeError(
                        f"Docker container exited early (status={container.status}).\n{logs}"
                    )

                frame_socket = _connect_socket(
                    Path(socket_directory) / "frame_capture.sock"
                )
                control_socket = _connect_socket(
                    Path(socket_directory) / "dolphin_control.sock"
                )
                memory_view = _resolve_memory_view(
                    container=container,
                    dev_shm_directory=Path(dev_shm_directory),
                    host_socket_path=memory_fd_socket_host,
                    container_socket_path=f"{self._container_socket_directory}/memory_fd.sock",
                )
                yield DockerDolphin.Session(
                    memory_view=memory_view,
                    frame_socket=frame_socket,
                    control_socket=control_socket,
                    driver=self._driver,
                )
            finally:
                memory_fd_socket_host.unlink(missing_ok=True)
                if frame_socket is not None:
                    frame_socket.sendall(b"Q")
                    frame_socket.close()
                if control_socket is not None:
                    control_socket.sendall(b"Q")
                    control_socket.close()
                container.remove(force=True)

    @abstractmethod
    def _container(
        self,
        socket_directory: Path,
        dev_shm_directory: Path,
    ) -> Container: ...


def _resolve_memory_view(
    container: Container,
    dev_shm_directory: Path,
    host_socket_path: Path,
    container_socket_path: str,
    timeout_sec: float = 30.0,
) -> DolphinMemoryView:
    deadline = time.monotonic() + timeout_sec
    with _use_host_unix_socket(host_socket_path) as host_unix_socket:
        while time.monotonic() < deadline:
            with _use_dolphin_memory_fd(
                container=container,
                container_socket_path=container_socket_path,
                host_unix_socket=host_unix_socket,
            ) as memory_fd:
                if memory_fd is not None:
                    try:
                        mapped = mmap.mmap(
                            memory_fd, 0, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ
                        )
                    finally:
                        os.close(memory_fd)
                    return DolphinMemoryView(memoryview(mapped))
            time.sleep(0.1)
    raise TimeoutError(
        f"Timed out waiting for Dolphin memory view in {dev_shm_directory}"
    )


def _connect_socket(socket_path: Path, timeout_sec: float = 30.0) -> socket.socket:
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
def _use_host_unix_socket(socket_path: Path) -> Iterator[socket.socket]:
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(socket_path))
    server.listen(1)
    server.settimeout(5.0)
    try:
        yield server
    finally:
        server.close()


@contextmanager
def _use_dolphin_memory_fd(
    container: Container,
    container_socket_path: str,
    host_unix_socket: socket.socket,
) -> Iterator[int | None]:
    result = container.exec_run(
        cmd=["python3", "-c", _DOLPHIN_MEMORY_FD_SCAN_SCRIPT, container_socket_path],
        stdout=True,
        stderr=True,
    )
    if result.exit_code == 11:
        yield None
        return
    if result.exit_code != 0:
        output = (
            result.output.decode("utf-8", errors="replace")
            if isinstance(result.output, bytes)
            else str(result.output)
        )
        raise RuntimeError(
            f"container pid/fd scan failed with exit_code={result.exit_code}: {output}"
        )

    conn, _ = host_unix_socket.accept()
    try:
        fds = array.array("i")
        msg, ancdata, _, _ = conn.recvmsg(16, socket.CMSG_LEN(struct.calcsize("i")))
        if msg != b"M":
            raise RuntimeError(f"unexpected memory-fd payload: {msg!r}")
        fd: int | None = None
        for cmsg_level, cmsg_type, cmsg_data in ancdata:
            if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
                fds.frombytes(cmsg_data[: struct.calcsize("i")])
                if fds:
                    fd = int(fds[0])
                    break
        if fd is None:
            raise RuntimeError("memory-fd transfer missing SCM_RIGHTS data")
        yield fd
    finally:
        conn.close()
