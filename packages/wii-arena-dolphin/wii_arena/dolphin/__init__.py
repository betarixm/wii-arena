from abc import ABC, abstractmethod
from types import CapsuleType
from typing import NewType

from wii_arena.core.environment.protocols import Environment
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
): ...
