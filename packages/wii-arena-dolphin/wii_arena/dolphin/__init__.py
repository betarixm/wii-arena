from abc import ABC, abstractmethod
from types import CapsuleType, TracebackType
from typing import NewType, Self

from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dlpack import (
    DlpackDevice,
    DlpackVersion,
    SupportsDlpack,
)

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack, ABC):
    @abstractmethod
    def __dlpack__(
        self,
        *,
        stream: object | None = None,
        max_version: DlpackVersion | None = None,
        dl_device: DlpackDevice | None = None,
        copy: bool | None = None,
    ) -> CapsuleType: ...

    @abstractmethod
    def __dlpack_device__(self) -> DlpackDevice: ...


class DolphinEnvironment[Action](
    Environment[tuple[DolphinMemoryView, DolphinFrameBuffer], Action, None], ABC
):
    def __init__(self, dolphin: Dolphin) -> None:
        self._dolphin = dolphin

    def reset(self) -> tuple[tuple[DolphinMemoryView, DolphinFrameBuffer], None]:
        return (self._dolphin.view_memory(), self._dolphin.view_frame_buffer()), None

    def step(
        self, action: Action
    ) -> tuple[
        tuple[DolphinMemoryView, DolphinFrameBuffer], Terminated, Truncated, None
    ]:
        self._execute_action(action)
        return (
            (self._dolphin.view_memory(), self._dolphin.view_frame_buffer()),
            Terminated(self._terminated()),
            Truncated(self._truncated()),
            None,
        )

    @abstractmethod
    def _execute_action(self, action: Action) -> None: ...

    @abstractmethod
    def _terminated(self) -> bool: ...

    @abstractmethod
    def _truncated(self) -> bool: ...


class Dolphin(ABC):
    @abstractmethod
    def view_memory(self) -> DolphinMemoryView: ...
    @abstractmethod
    def view_frame_buffer(self) -> DolphinFrameBuffer: ...
    @abstractmethod
    def __enter__(self) -> Self: ...
    @abstractmethod
    def __exit__(
        self,
        exc_type: type[BaseException],
        exc: BaseException | None,
        tb: TracebackType | None,
    ): ...
