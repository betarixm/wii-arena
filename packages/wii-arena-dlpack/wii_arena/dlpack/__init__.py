from abc import ABC, abstractmethod
from contextlib import AbstractContextManager
from enum import IntEnum
from types import CapsuleType
from typing import Protocol, TypeAlias


class DlpackDeviceType(IntEnum):
    CPU = 1
    CUDA = 2
    CPU_PINNED = 3
    OPENCL = 4
    VULKAN = 7
    METAL = 8
    VPI = 9
    ROCM = 10
    CUDA_MANAGED = 13
    ONE_API = 14


DlpackDevice: TypeAlias = tuple[DlpackDeviceType, int]
DlpackVersion: TypeAlias = tuple[int, int]


class SupportsDlpack(Protocol):
    def __dlpack__(
        self,
        *,
        stream: object | None = None,
        max_version: DlpackVersion | None = None,
        dl_device: DlpackDevice | None = None,
        copy: bool | None = None,
    ) -> CapsuleType: ...

    def __dlpack_device__(self) -> DlpackDevice: ...


class Driver(ABC):
    @abstractmethod
    def dlpack_from_file_descriptor(
        self, file_descriptor: int, size: int, height: int, stride: int
    ) -> AbstractContextManager[SupportsDlpack]: ...
