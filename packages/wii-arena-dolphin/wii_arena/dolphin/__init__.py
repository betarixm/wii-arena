from abc import ABC, abstractmethod
from contextlib import contextmanager
from types import TracebackType
from typing import Iterator, NewType

from wii_arena.core.arena.protocols import Arena
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.provision.protocols import Provision
from wii_arena.dlpack import SupportsDlpack

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack): ...


class Dolphin(ABC):
    @abstractmethod
    def view_memory(self) -> DolphinMemoryView: ...
    @abstractmethod
    def view_frame_buffer(self) -> DolphinFrameBuffer: ...

    @abstractmethod
    def __enter__(
        self,
    ): ...  # Prologue of the process, e.g. booting up.
    @abstractmethod
    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ): ...  # Epilogue of the process, e.g. closing the process.


class DolphinScenario(ABC):
    def __init__(self, dolphin: Dolphin):
        self._dolphin = dolphin

    @property
    def dolphin(self) -> Dolphin:
        return self._dolphin

    @abstractmethod
    def __enter__(
        self,
    ): ...  # Prologue of the game, e.g. navigating menus.

    @abstractmethod
    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ): ...  # Epilogue of the game, e.g. returning to the main menu or closing the game.

    @abstractmethod
    def terminated(self) -> Terminated: ...
    @abstractmethod
    def truncated(self) -> Truncated: ...


class DolphinEnvironment[Action](
    Environment[tuple[DolphinMemoryView, DolphinFrameBuffer], Action, None]
):
    def __init__(self, dolphin_scenario: DolphinScenario) -> None:
        self._dolphin_scenario = dolphin_scenario

    def reset(self) -> tuple[tuple[DolphinMemoryView, DolphinFrameBuffer], None]:
        return (
            self._dolphin_scenario.dolphin.view_memory(),
            self._dolphin_scenario.dolphin.view_frame_buffer(),
        ), None

    def step(
        self, action: Action
    ) -> tuple[
        tuple[DolphinMemoryView, DolphinFrameBuffer], Terminated, Truncated, None
    ]:
        self._execute_action(action)
        return (
            (
                self._dolphin_scenario.dolphin.view_memory(),
                self._dolphin_scenario.dolphin.view_frame_buffer(),
            ),
            Terminated(self._dolphin_scenario.terminated()),
            Truncated(self._dolphin_scenario.truncated()),
            None,
        )

    def _execute_action(self, action: Action) -> None:
        # TODO: delegate to `Dolphin` to handle these.
        ...


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


class DolphinUniverse[Action](Provision[DolphinEnvironment[Action]]):
    def __init__(self, author: Provision[DolphinScenario]):
        self._author = author

    @contextmanager
    def session(self) -> Iterator[DolphinEnvironment[Action]]:
        with self._author.session() as dolphin_scenario:
            yield DolphinEnvironment(dolphin_scenario)
