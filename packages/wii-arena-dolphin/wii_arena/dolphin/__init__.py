from abc import ABC, abstractmethod
from contextlib import contextmanager
from enum import Enum
from typing import Iterator, NewType

from wii_arena.core.arena.protocols import Arena
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.provision.protocols import Provision
from wii_arena.dlpack import SupportsDlpack

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack): ...


class DolphinAction(
    Enum
): ...  # TODO: define the actions that can be taken in the Dolphin environment, e.g. button presses, joystick movements, etc.


class Dolphin(ABC):
    @abstractmethod
    def execute(self, action: DolphinAction) -> None: ...
    @abstractmethod
    def memory_view(self) -> DolphinMemoryView: ...
    @abstractmethod
    def frame_buffer(self) -> DolphinFrameBuffer: ...


class DolphinScenario(ABC):
    def __init__(self, dolphin: Dolphin):
        self._dolphin = dolphin

    @property
    def dolphin(self) -> Dolphin:
        return self._dolphin

    @abstractmethod
    def terminated(self) -> Terminated: ...
    @abstractmethod
    def truncated(self) -> Truncated: ...


class DolphinScenarioAuthor(Provision[DolphinScenario]): ...


class DolphinEnvironment(
    Environment[tuple[DolphinMemoryView, DolphinFrameBuffer], DolphinAction, None]
):
    def __init__(self, dolphin_scenario: DolphinScenario) -> None:
        self._dolphin_scenario = dolphin_scenario

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


class DolphinUniverse(Provision[DolphinEnvironment]):
    def __init__(self, author: DolphinScenarioAuthor):
        self._author = author

    @contextmanager
    def session(self) -> Iterator[DolphinEnvironment]:
        with self._author.session() as scenario:
            yield DolphinEnvironment(dolphin_scenario=scenario)


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
