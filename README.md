<div align="center">

# 👾<br>Wii Arena

**Press <kbd>A</kbd> to continue.**

Bringing Wii Environments to AI Agents.
</div>

```python
from pathlib import Path

import docker
from wii_arena.core.agent.protocols import Agent
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import DolphinEnvironment
from wii_arena.dolphin_docker import DockerDolphin
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenario

DOCKER_IMAGE = docker.from_env().images.get("ghcr.io/betarixm/wii-arena-dolphin:latest")
ISO_FILE: Path = ...
AGENT: Agent = ...

with DolphinEnvironment(
    scenario=MarioKartWiiGrandPrixScenario(
        dolphin=DockerDolphin(docker_image=DOCKER_IMAGE, wii_iso_file=ISO_FILE)
    )
).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = AGENT.act(observation)
        observation, terminated, truncated, context = environment.step(action=action)
```

## Quickstart

```bash
git clone https://github.com/2026-poka-science-war-ai/environment.git
cd environment
uv sync
```

```bash
docker pull ghcr.io/betarixm/wii-arena-dolphin:latest
```

## Environment Behavior

### Execution Model

The environment runs synchronously. It waits for the agent to return an action before advancing to the next step.

### Observation Semantics

The environment returns a memory view, not a memory copy. Therefore, agents must not mutate the observation returned by the environment. The environment does not enforce immutability at the type or runtime level. However, because execution is synchronous, participants may treat observations as immutable within each step.

## Limitations

### CUDA Support

Running the environment with `DockerDolphin` and `ghcr.io/betarixm/wii-arena-dolphin` is supported only on Linux with CUDA.
