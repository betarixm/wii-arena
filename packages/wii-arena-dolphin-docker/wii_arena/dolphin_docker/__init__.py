from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator

from docker.models.images import Image
from wii_arena.core.provision.protocols import Provision
from wii_arena.dolphin import (
    Dolphin,
    DolphinAction,
    DolphinFrameBuffer,
    DolphinMemoryView,
)


class DockerDolphin(Dolphin):
    def __init__(self, memory_view: DolphinMemoryView):
        self._memory_view = memory_view

    def execute(self, action: DolphinAction) -> None:
        # TODO: implement this method to execute the given action in the Dolphin emulator running in the Docker container.
        ...

    def memory_view(self) -> DolphinMemoryView:
        return self._memory_view

    def frame_buffer(self) -> DolphinFrameBuffer:
        # TODO: implement this method to return a view of the frame buffer from the Dolphin emulator running in the Docker container.
        ...


class DockerDolphinLauncher(Provision[DockerDolphin]):
    def __init__(self, docker_image: Image):
        self._docker_image = docker_image

    @contextmanager
    def session(self) -> Iterator[DockerDolphin]:
        # TODO: spawn a Docker container from the given image, start the Dolphin emulator inside it, and yield a DockerDolphin instance that can be used to interact with the emulator.
        ...
        # TODO: Make sure to clean up the container after use.
