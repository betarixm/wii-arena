from __future__ import annotations

from abc import ABC, abstractmethod
from contextlib import AbstractContextManager, contextmanager
from enum import Enum
from typing import Iterator, NewType

from wii_arena.core.arena.protocols import Arena
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.session.protocols import SupportsSession
from wii_arena.dlpack import SupportsDlpack

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack): ...


class DolphinAction(
    Enum
): ...  # TODO: define the actions that can be taken in the Dolphin environment, e.g. button presses, joystick movements, etc.


class Dolphin(SupportsSession):
    class Session(ABC, SupportsSession.Session):
        @abstractmethod
        def execute(self, action: DolphinAction) -> None: ...
        @abstractmethod
        def memory_view(self) -> DolphinMemoryView: ...
        @abstractmethod
        def frame_buffer(self) -> DolphinFrameBuffer: ...

    @abstractmethod
    def session(self) -> AbstractContextManager[Dolphin.Session]: ...


class DolphinScenario(SupportsSession, ABC):
    class Session(ABC, SupportsSession.Session):
        def __init__(self, dolphin_session: Dolphin.Session):
            self._dolphin = dolphin_session

        @property
        def dolphin(self) -> Dolphin.Session:
            return self._dolphin

        @abstractmethod
        def terminated(self) -> Terminated: ...
        @abstractmethod
        def truncated(self) -> Truncated: ...

    @abstractmethod
    def session(
        self,
    ) -> AbstractContextManager[DolphinScenario.Session, bool | None]: ...


class DolphinEnvironment(
    Environment[tuple[DolphinMemoryView, DolphinFrameBuffer], DolphinAction, None]
):
    class Session(
        Environment.Session[
            tuple[DolphinMemoryView, DolphinFrameBuffer], DolphinAction, None
        ],
        SupportsSession.Session,
    ):
        def __init__(self, scenario_session: DolphinScenario.Session) -> None:
            self._dolphin_scenario = scenario_session

        def reset(self) -> tuple[tuple[DolphinMemoryView, DolphinFrameBuffer], None]:
            return (
                self._dolphin_scenario.dolphin.memory_view(),
                self._dolphin_scenario.dolphin.frame_buffer(),
            ), None

        def step(
            self, action: DolphinAction
        ) -> tuple[
            tuple[DolphinMemoryView, DolphinFrameBuffer], Terminated, Truncated, None
        ]:
            self._dolphin_scenario.dolphin.execute(action)
            return (
                (
                    self._dolphin_scenario.dolphin.memory_view(),
                    self._dolphin_scenario.dolphin.frame_buffer(),
                ),
                Terminated(self._dolphin_scenario.terminated()),
                Truncated(self._dolphin_scenario.truncated()),
                None,
            )

    def __init__(self, scenario: DolphinScenario):
        self._scenario = scenario

    @contextmanager
    def session(self) -> Iterator[Session]:
        with self._scenario.session() as scenario_session:
            yield DolphinEnvironment.Session(scenario_session=scenario_session)


class DolphinArena[Action](
    Arena[
        DolphinFrameBuffer,
        tuple[DolphinMemoryView, DolphinFrameBuffer],
        Action,
        None,
    ]
):
    def _event_from_observation(
        self,
        observation: tuple[DolphinMemoryView, DolphinFrameBuffer],
        context: None,
    ) -> DolphinFrameBuffer:
        return observation[1]
