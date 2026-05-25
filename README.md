# Wii Arena

```python
import docker
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import DolphinUniverse
from wii_arena.dolphin_docker import DockerDolphinLauncher
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenarioAuthor

DOCKER_IMAGE = docker.from_env().images.get("...")

with DolphinUniverse(
    author=MarioKartWiiGrandPrixScenarioAuthor(
        dolphin_launcher=DockerDolphinLauncher(docker_image=DOCKER_IMAGE)
    )
).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = agent.act(observation)
        observation, terminated, truncated, context = environment.step(action)

```
