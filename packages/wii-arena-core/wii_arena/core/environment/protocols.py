from __future__ import annotations

from abc import ABC, abstractmethod
from contextlib import AbstractContextManager

from ..session.protocols import SupportsSession
from .types import Terminated, Truncated


class Environment[Observation, Action, Context](SupportsSession, ABC):
    class Session[_Observation, _Action, _Context](ABC, SupportsSession.Session):
        @abstractmethod
        def step(
            self, action: _Action
        ) -> tuple[_Observation, Terminated, Truncated, _Context]: ...

        @abstractmethod
        def reset(self) -> tuple[_Observation, _Context]: ...

    @abstractmethod
    def session(
        self,
    ) -> AbstractContextManager[Environment.Session[Observation, Action, Context]]: ...
