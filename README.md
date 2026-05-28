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
from wii_arena.dolphin import DolphinEnvironment
from wii_arena.dolphin_docker_nvidia import NvidiaDockerDolphin
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenario

DOCKER_IMAGE = docker.from_env().images.get("ghcr.io/betarixm/wii-arena-dolphin:latest")
ISO_FILE: Path = ...
AGENT: Agent = ...

with DolphinEnvironment(
    scenario=MarioKartWiiGrandPrixScenario(
        dolphin=NvidiaDockerDolphin(docker_image=DOCKER_IMAGE, wii_iso_file=ISO_FILE)
    )
).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = AGENT.act(observation)
        observation, terminated, truncated, context = environment.step(action=action)
```

### Using Local Environment with NVIDIA Support

```bash
apt update
apt install libbluetooth3 libhidapi-hidraw0 libspng0 libpugixml1v5 libqt6core6t64 libqt6dbus6t64
```

```bash
wget "https://.../wii-arena-dolphin-linux-x86_64.zip" # Download from GitHub Actions Artifacts
wget "https://.../wii-arena-vulkan-layer-linux-x86_64.zip"  # Download from GitHub Actions Artifacts
unzip ...
```

```bash
uv add "git+https://github.com/betarixm/wii-arena.git#subdirectory=packages/wii-arena-cuda-driver"
uv add "git+https://github.com/betarixm/wii-arena.git#subdirectory=packages/wii-arena-dolphin-local"
```

```python
from wii_arena.core.agent.protocols import Agent
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.cuda_driver import CudaDriver
from wii_arena.dolphin import DolphinEnvironment
from wii_arena.dolphin_local import LocalDolphin
from wii_arena.mario_kart import MarioKartWiiGrandPrixScenario

AGENT: Agent = ...

with DolphinEnvironment(
    scenario=MarioKartWiiGrandPrixScenario(
        dolphin=LocalDolphin(
            executable_path=DOLPHIN_EXECUTABLE,
            vulcan_layer_path=VULKAN_LAYER_LIBRARY,
            vulkan_layer_configuration_path=VULKAN_LAYER_CONFIG,
            wii_iso_file=ISO_FILE,
            driver=CudaDriver(),
        )
    )
).session() as environment:
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
