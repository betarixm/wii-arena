<div align="center">

# 👾<br>Wii Arena

**Press <kbd>A</kbd> to continue.**

Bringing Wii Environments to AI Agents.
</div>

```python
import docker
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import DolphinEnvironment
from wii_arena.dolphin_docker import DockerDolphin
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenario

DOCKER_IMAGE = docker.from_env().images.get("...")

with DolphinEnvironment(
    scenario=MarioKartWiiGrandPrixScenario(
        dolphin=DockerDolphin(docker_image=DOCKER_IMAGE)
    )
).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = agent.act(observation)
        observation, terminated, truncated, context = environment.step(action)
```
