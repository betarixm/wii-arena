from __future__ import annotations

import logging
import socket
import struct
from abc import ABC, abstractmethod
from contextlib import AbstractContextManager, ExitStack, contextmanager
from types import CapsuleType
from typing import Iterator, NewType

from pydantic import BaseModel, Field
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.session.protocols import SupportsSession
from wii_arena.dlpack import DlpackDevice, DlpackVersion, SupportsDlpack

_LOGGER = logging.getLogger(__name__)

_INITIAL_FRAME_ATTEMPTS = 600

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack):
    def __init__(
        self,
        frame: SupportsDlpack,
        *,
        width: int,
        height: int,
        stride: int,
        size: int,
        frame_id: int,
        frame_format: int,
    ) -> None:
        self._frame = frame
        self.width = width
        self.height = height
        self.stride = stride
        self.size = size
        self.frame_id = frame_id
        self.frame_format = frame_format

    def __dlpack__(
        self,
        *,
        stream: object | None = None,
        max_version: DlpackVersion | None = None,
        dl_device: DlpackDevice | None = None,
        copy: bool | None = None,
    ) -> CapsuleType:
        return self._frame.__dlpack__(
            stream=stream,
            max_version=max_version,
            dl_device=dl_device,
            copy=copy,
        )

    def __dlpack_device__(self) -> DlpackDevice:
        return self._frame.__dlpack_device__()


class DolphinFrameBufferUnavailable(RuntimeError): ...


class DolphinGameCubeControllerInput(BaseModel):
    a: bool = False
    b: bool = False
    x: bool = False
    y: bool = False
    z: bool = False
    start: bool = False
    up: bool = False
    down: bool = False
    left: bool = False
    right: bool = False
    l: bool = False
    r: bool = False
    stick_x: float = Field(default=0.0, ge=-1.0, le=1.0)
    stick_y: float = Field(default=0.0, ge=-1.0, le=1.0)
    c_stick_x: float = Field(default=0.0, ge=-1.0, le=1.0)
    c_stick_y: float = Field(default=0.0, ge=-1.0, le=1.0)
    trigger_left: float = Field(default=0.0, ge=0.0, le=1.0)
    trigger_right: float = Field(default=0.0, ge=0.0, le=1.0)


class DolphinGameCubeControllerNoOp(BaseModel): ...


DolphinAction = list[DolphinGameCubeControllerInput | DolphinGameCubeControllerNoOp]

_OVERRIDE_FLAG = 1 << 15


class Dolphin(SupportsSession):
    class Session(ABC, SupportsSession.Session):
        _control_socket: socket.socket

        def execute(self, action: DolphinAction) -> None:
            _LOGGER.debug("Executing action=%s in Dolphin session", action)
            self._control_socket.sendall(b"E" + self._pack_action(action))
            if self._control_socket.recv(1) != b"D":
                raise RuntimeError(
                    f"Unexpected response from Dolphin control socket: {self._control_socket.recv(1024)}"
                )

        @abstractmethod
        def memory_view(self) -> DolphinMemoryView: ...
        def frame_buffer(self) -> AbstractContextManager[DolphinFrameBuffer]: ...

        @staticmethod
        def _pack_action(action: DolphinAction) -> bytes:
            if len(action) != 4:
                raise ValueError("DolphinAction requires inputs for all 4 controllers.")

            format_string = "<" + ("H6f" * len(action))

            data: list[int | float] = []
            for controller_input in action:
                if isinstance(controller_input, DolphinGameCubeControllerNoOp):
                    data.append(0)
                    data.extend([0.0] * 6)
                    continue

                button_mask = 0
                for j, btn in enumerate(
                    [
                        "a",
                        "b",
                        "x",
                        "y",
                        "z",
                        "start",
                        "up",
                        "down",
                        "left",
                        "right",
                        "l",
                        "r",
                    ]
                ):
                    if getattr(controller_input, btn):
                        button_mask |= 1 << j
                data.append(_OVERRIDE_FLAG | button_mask)
                data.extend(
                    [
                        controller_input.stick_x,
                        controller_input.stick_y,
                        controller_input.c_stick_x,
                        controller_input.c_stick_y,
                        controller_input.trigger_left,
                        controller_input.trigger_right,
                    ]
                )

            return struct.pack(format_string, *data)

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
            frame_buffer: DolphinFrameBuffer | None = None
            last_unavailable: DolphinFrameBufferUnavailable | None = None
            for _ in range(_INITIAL_FRAME_ATTEMPTS):
                self._dolphin_scenario.dolphin.execute(
                    [
                        DolphinGameCubeControllerNoOp(),
                        DolphinGameCubeControllerNoOp(),
                        DolphinGameCubeControllerNoOp(),
                        DolphinGameCubeControllerNoOp(),
                    ]
                )
                try:
                    frame_buffer = self._refresh_frame_buffer()
                    break
                except DolphinFrameBufferUnavailable as error:
                    last_unavailable = error
                    continue
            else:
                details = (
                    f" Last layer status: {last_unavailable}"
                    if last_unavailable is not None
                    else ""
                )
                raise TimeoutError(
                    f"Timed out waiting for initial Dolphin frame buffer.{details}"
                )

            assert frame_buffer is not None
            return (
                self._dolphin_scenario.dolphin.memory_view(),
                frame_buffer,
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
