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
        container_socket_directory: str = "/tmp",
    ):
        super().__init__(
            docker_image=docker_image,
            wii_iso_file=wii_iso_file,
            driver=CudaDriver(),
            docker_client=docker_client,
            container_socket_directory=container_socket_directory,
        )

    def _container(
        self,
        socket_directory: Path,
        dev_shm_directory: Path,
    ) -> Container:
        configuration_path = Path(__file__).parents[4] / "configurations" / "dolphin"
        script_path = configuration_path / "controller.py"
        print(script_path)
        if not script_path.is_file():
            raise FileNotFoundError(f"Controller script not found at {script_path}")
        print(socket_directory)
        print(socket_directory)
        print(socket_directory)
        print(socket_directory)
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
                str(script_path): {"bind": "/controller.py", "mode": "ro"},
            },
            environment={
                "NVIDIA_VISIBLE_DEVICES": "all",
                "NVIDIA_DRIVER_CAPABILITIES": "all",
                "FRAME_CAPTURE_SOCKET": self._container_frame_socket_file,
                "CONTROL_SOCKET": self._container_control_socket_file,
            },
            command=[
                "dolphin-emu-nogui",
                "--exec=/game.iso",
                "--platform=headless",
                "--video_backend=Vulkan",
                "--user=/dolphin-user",
                "--script=/controller.py",
            ],
        )
