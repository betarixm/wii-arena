from pathlib import Path

from docker import DockerClient
from docker.models.containers import Container
from docker.models.images import Image
from wii_arena.cuda_driver import CudaDriver
from wii_arena.dolphin_docker import DockerDolphin


class NvidiaDockerDolphin(DockerDolphin):
    def __init__(
        self,
        docker_image: Image,
        wii_iso_file: Path,
        docker_client: DockerClient | None = None,
        container_socket_directory: str = "/run/wii-arena",
        extra_volumes: dict[str, dict[str, str]] | None = None,
        extra_dolphin_arguments: list[str] | None = None,
        gpu: str = "0",
    ):
        super().__init__(
            docker_image=docker_image,
            wii_iso_file=wii_iso_file,
            driver=CudaDriver(),
            docker_client=docker_client,
            container_socket_directory=container_socket_directory,
            extra_volumes=extra_volumes,
            extra_dolphin_arguments=extra_dolphin_arguments,
        )
        self._gpu = gpu

    def _container(
        self,
        socket_directory: Path,
        dev_shm_directory: Path,
    ) -> Container:
        return self._docker_client.containers.run(
            image=self._docker_image,
            detach=True,
            remove=False,
            runtime="nvidia",
            shm_size="2g",
            working_dir="/workspace",
            tty=True,
            volumes={
                str(self._wii_iso_file): {"bind": "/game.iso", "mode": "ro"},
                str(socket_directory): {
                    "bind": self._container_socket_directory,
                    "mode": "rw",
                },
                str(dev_shm_directory): {"bind": "/dev/shm", "mode": "rw"},
                **self._extra_volumes,
            },
            environment={
                "NVIDIA_VISIBLE_DEVICES": self._gpu,
                "NVIDIA_DRIVER_CAPABILITIES": "all",
                "FRAME_CAPTURE_SOCKET": self._container_frame_socket_file,
                "CONTROL_SOCKET": self._container_control_socket_file,
            },
            command=[
                "/opt/wii-arena/bin/entrypoint",
                "dolphin-emu-nogui",
                "--exec=/game.iso",
                "--platform=x11",
                "--video_backend=Vulkan",
                "--user=/dolphin-user",
                "--script=/scripts/control.py",
                *self._extra_dolphin_arguments,
            ],
        )
