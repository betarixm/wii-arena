from __future__ import annotations

from docker.models.images import Image
from wii_arena.dolphin import (
    Dolphin,
    DolphinAction,
    DolphinFrameBuffer,
    DolphinMemoryView,
)


class DockerDolphin(Dolphin):
    @staticmethod
    def from_docker_image(docker_image: Image) -> DockerDolphin:
        # TODO: Spawn a Docker container from the given Docker image.
        # TODO: Get a memory view of the Dolphin emulator running in the Docker container.
        # TODO: Get a frame buffer view of the Dolphin emulator running in the Docker container.
        ...

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
