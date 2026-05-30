from __future__ import annotations

import logging
from abc import ABC, abstractmethod
from contextlib import AbstractContextManager, ExitStack, contextmanager
from typing import Iterator, NewType

from pydantic import BaseModel, Field
from wii_arena.core.arena.protocols import Arena
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.session.protocols import SupportsSession
from wii_arena.dlpack import SupportsDlpack

_LOGGER = logging.getLogger(__name__)

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack): ...


class DolphinAgentAction(BaseModel):
    A: bool = False
    B: bool = False
    X: bool = False
    Y: bool = False
    Z: bool = False
    Start: bool = False
    Up: bool = False
    Down: bool = False
    Left: bool = False
    Right: bool = False
    L: bool = False
    R: bool = False
    StickX: float = Field(default=0.0, ge=-1.0, le=1.0)
    StickY: float = Field(default=0.0, ge=-1.0, le=1.0)
    CStickX: float = Field(default=0.0, ge=-1.0, le=1.0)
    CStickY: float = Field(default=0.0, ge=-1.0, le=1.0)
    TriggerLeft: float = Field(default=0.0, ge=0.0, le=1.0)
    TriggerRight: float = Field(default=0.0, ge=0.0, le=1.0)


DolphinAgentIndex = NewType("DolphinAgentIndex", int)
DolphinAction = list[tuple[DolphinAgentIndex, DolphinAgentAction]]


class Dolphin(SupportsSession):
    class Session(ABC, SupportsSession.Session):
        @abstractmethod
        def execute(self, action: DolphinAction) -> None: ...
        @abstractmethod
        def memory_view(self) -> DolphinMemoryView: ...
        @abstractmethod
        def frame_buffer(self) -> AbstractContextManager[DolphinFrameBuffer]: ...

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
            self.frame_stack = ExitStack()
            _LOGGER.debug("Created DolphinEnvironment.Session")

        def _refresh_frame_buffer(self) -> DolphinFrameBuffer:
            _LOGGER.debug("Refreshing frame buffer context")
            self.frame_stack.close()
            self.frame_stack = ExitStack()
            return self.frame_stack.enter_context(
                self._dolphin_scenario.dolphin.frame_buffer()
            )

        def reset(self) -> tuple[tuple[DolphinMemoryView, DolphinFrameBuffer], None]:
            _LOGGER.info("Resetting Dolphin environment session")
            return (
                self._dolphin_scenario.dolphin.memory_view(),
                self._refresh_frame_buffer(),
            ), None

        def step(
            self, action: DolphinAction
        ) -> tuple[
            tuple[DolphinMemoryView, DolphinFrameBuffer], Terminated, Truncated, None
        ]:
            _LOGGER.debug("Stepping Dolphin environment with action=%s", action)
            self._dolphin_scenario.dolphin.execute(action)
            return (
                (
                    self._dolphin_scenario.dolphin.memory_view(),
                    self._refresh_frame_buffer(),
                ),
                Terminated(self._dolphin_scenario.terminated()),
                Truncated(self._dolphin_scenario.truncated()),
                None,
            )

    def __init__(self, scenario: DolphinScenario):
        self._scenario = scenario
        _LOGGER.debug(
            "Initialized DolphinEnvironment with scenario=%s", type(scenario).__name__
        )

    @contextmanager
    def session(self) -> Iterator[Session]:
        _LOGGER.info("Opening DolphinEnvironment session")
        with self._scenario.session() as scenario_session:
            session = DolphinEnvironment.Session(scenario_session=scenario_session)
            try:
                yield session
            finally:
                _LOGGER.info("Closing DolphinEnvironment session")
                session.frame_stack.close()


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
