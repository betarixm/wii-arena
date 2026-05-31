from __future__ import annotations

import ctypes
import os
from contextlib import contextmanager
from typing import Any, Iterator, cast

import cupy  # pyright: ignore[reportMissingTypeStubs]
from wii_arena.dlpack import Driver, SupportsDlpack

_CUPY = cast(Any, cupy)
_CUPY_CUDA = cast(Any, cupy.cuda)


class _CudaWin32Handle(ctypes.Structure):
    _fields_ = [("handle", ctypes.c_void_p), ("name", ctypes.c_void_p)]


class _CudaExternalHandle(ctypes.Union):
    _fields_ = [
        ("fd", ctypes.c_int),
        ("win32", _CudaWin32Handle),
        ("nvSciBufObject", ctypes.c_void_p),
    ]


class _CudaExternalMemoryHandleDescriptor(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("handle", _CudaExternalHandle),
        ("size", ctypes.c_ulonglong),
        ("flags", ctypes.c_uint),
        ("reserved", ctypes.c_uint * 16),
    ]


class _CudaExternalMemoryBufferDescriptor(ctypes.Structure):
    _fields_ = [
        ("offset", ctypes.c_ulonglong),
        ("size", ctypes.c_ulonglong),
        ("flags", ctypes.c_uint),
        ("reserved", ctypes.c_uint * 16),
    ]


class _CudaRuntime:
    def __init__(self) -> None:
        self.lib = ctypes.CDLL("libcuda.so.1")
        self.lib.cuInit.argtypes = [ctypes.c_uint]
        self.lib.cuDeviceGet.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_int]
        self.cu_ctx_create = getattr(self.lib, "cuCtxCreate_v2", self.lib.cuCtxCreate)
        self.cu_ctx_create.argtypes = [
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_uint,
            ctypes.c_int,
        ]
        self.lib.cuImportExternalMemory.argtypes = [
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(_CudaExternalMemoryHandleDescriptor),
        ]
        self.lib.cuExternalMemoryGetMappedBuffer.argtypes = [
            ctypes.POINTER(ctypes.c_ulonglong),
            ctypes.c_void_p,
            ctypes.POINTER(_CudaExternalMemoryBufferDescriptor),
        ]
        self.lib.cuDestroyExternalMemory.argtypes = [ctypes.c_void_p]
        self.lib.cuInit(0)
        device = ctypes.c_int()
        self.lib.cuDeviceGet(ctypes.byref(device), 0)
        context = ctypes.c_void_p()
        self.cu_ctx_create(ctypes.byref(context), 0, device)
        self.context = context


class CudaDriver(Driver):
    def __init__(self, cuda_runtime: _CudaRuntime | None = None) -> None:
        self._runtime = cuda_runtime if cuda_runtime is not None else _CudaRuntime()

    @contextmanager
    def dlpack_from_file_descriptor(
        self, file_descriptor: int, size: int, height: int, stride: int
    ) -> Iterator[SupportsDlpack]:

        desc = _CudaExternalMemoryHandleDescriptor()
        desc.type = 1  # CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD
        desc.handle.fd = file_descriptor
        desc.size = size
        desc.flags = 0  # Vulkan buffer memory is not dedicated.
        ext = ctypes.c_void_p()
        return_code = self._runtime.lib.cuImportExternalMemory(
            ctypes.byref(ext), ctypes.byref(desc)
        )
        if return_code != 0:
            raise RuntimeError(f"cuImportExternalMemory failed: {return_code}")

        try:
            buffer_descriptor = _CudaExternalMemoryBufferDescriptor()
            buffer_descriptor.offset = 0
            buffer_descriptor.size = size
            pointer = ctypes.c_ulonglong()
            return_code = self._runtime.lib.cuExternalMemoryGetMappedBuffer(
                ctypes.byref(pointer),
                ext,
                ctypes.byref(buffer_descriptor),
            )
            if return_code != 0:
                raise RuntimeError(
                    f"cuExternalMemoryGetMappedBuffer failed: {return_code}"
                )

            # Ownership of file_descriptor is transferred to CUDA on successful import.
            file_descriptor = -1
            unowned = _CUPY_CUDA.UnownedMemory(pointer.value, size, owner=ext)
            memptr = _CUPY_CUDA.MemoryPointer(unowned, 0)
            cupy_array = _CUPY.ndarray(
                (height, stride // 4, 4),
                dtype=_CUPY.uint8,
                memptr=memptr,
            )
            yield cupy_array
        finally:
            if ext.value:
                self._runtime.lib.cuDestroyExternalMemory(ext)
            if file_descriptor >= 0:
                os.close(file_descriptor)
