import logging
from pathlib import Path

import docker
import PIL.Image as Image
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import DolphinAction, DolphinEnvironment
from wii_arena.dolphin_docker_nvidia import NvidiaDockerDolphin
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenario

logging.basicConfig(level=logging.DEBUG)
DOCKER_IMAGE = docker.from_env().images.get("ghcr.io/betarixm/wii-arena-dolphin:latest")
ISO_FILE: Path = Path("/public/MarioKartWii.iso")

with DolphinEnvironment(
    scenario=MarioKartWiiGrandPrixScenario(
        dolphin=NvidiaDockerDolphin(docker_image=DOCKER_IMAGE, wii_iso_file=ISO_FILE)
    )
).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)
    for _ in range(1000):
        action = {}
        observation, terminated, truncated, info = environment.step(DolphinAction())
        if _ % 100 == 0:
            import cupy

            frame_buffer = observation[1]
            frame_buffer_array = cupy.from_dlpack(frame_buffer)
            frame_buffer_array = cupy.asnumpy(frame_buffer_array)
            image = Image.fromarray(frame_buffer_array, mode="RGBA")
            image.save(f"frame_{_}.png")
        if terminated or truncated:
            break

    __import__("time").sleep(5000000)
