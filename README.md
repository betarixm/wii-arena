<div align="center">

# 👾<br>Wii Arena

**Press <kbd>A</kbd> to continue.**

Bringing Wii Environments to AI Agents.
</div>

## Quickstart

### Using Docker Environment with NVIDIA Support

```bash
docker pull ghcr.io/betarixm/wii-arena-dolphin:latest

uv add git+https://github.com/betarixm/wii-arena.git#subdirectory=packages/wii-arena-dolphin-docker-nvidia
```

```python
from pathlib import Path

import docker
from wii_arena.core.agent.protocols import Agent
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import DolphinEnvironment, DolphinScenario
from wii_arena.dolphin_docker_nvidia import NvidiaDockerDolphin

DOCKER_IMAGE = docker.from_env().images.get("ghcr.io/betarixm/wii-arena-dolphin:latest")
ISO_FILE: Path = ...
AGENT: Agent = ...
SCENARIO: DolphinScenario = ...

with DolphinEnvironment(scenario=SCENARIO).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = AGENT.act(observation)
        observation, terminated, truncated, context = environment.step(action=action)
```

## Environment Behavior

### Execution Model

The environment runs synchronously. It waits for the agent to return an action before advancing to the next step.

### Observation Semantics

The environment returns a memory view, not a memory copy. Therefore, agents must not mutate the observation returned by the environment. The environment does not enforce immutability at the type or runtime level. However, because execution is synchronous, participants may treat observations as immutable within each step.
