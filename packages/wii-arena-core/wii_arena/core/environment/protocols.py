from abc import ABC, abstractmethod

from .types import Terminated, Truncated


class Environment[Observation, Action, Context](ABC):
    @abstractmethod
    def step(
        self, action: Action
    ) -> tuple[Observation, Terminated, Truncated, Context]: ...

    @abstractmethod
    def reset(self) -> tuple[Observation, Context]: ...
