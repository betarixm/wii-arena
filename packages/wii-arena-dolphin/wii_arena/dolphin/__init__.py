from __future__ import annotations

import array
import logging
import os
import socket
import struct
from abc import ABC, abstractmethod
from contextlib import AbstractContextManager, ExitStack, contextmanager
from typing import Iterator, NewType, cast

from pydantic import BaseModel, Field
from wii_arena.core.arena.protocols import Arena
from wii_arena.core.environment.protocols import Environment
from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.session.protocols import SupportsSession
from wii_arena.dlpack import Driver, SupportsDlpack

_LOGGER = logging.getLogger(__name__)

_INITIAL_FRAME_ATTEMPTS = 600

DolphinMemoryView = NewType("DolphinMemoryView", memoryview)


class DolphinFrameBuffer(SupportsDlpack):
    width: int
    height: int
    stride: int
    size: int
    frame_id: int
    frame_format: int


class DolphinFrameBufferHandle:
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

    def __dlpack__(self, **kwargs: object) -> object:
        return self._frame.__dlpack__(**kwargs)  # type: ignore[arg-type]

    def __dlpack_device__(self) -> object:
        return self._frame.__dlpack_device__()


class DolphinFrameBufferUnavailable(RuntimeError): ...


@contextmanager
def receive_frame_buffer(
    frame_socket: socket.socket, driver: Driver
) -> Iterator[DolphinFrameBuffer]:
    frame_socket.sendall(b"R")
    fds = array.array("i")
    msg, ancdata, _, _ = frame_socket.recvmsg(
        4096, socket.CMSG_LEN(struct.calcsize("i"))
    )
    if not msg:
        raise RuntimeError("Frame socket closed before sending a frame packet.")
    if msg == b"N":
        raise DolphinFrameBufferUnavailable("Layer does not have a captured frame yet.")

    fd: int | None = None
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds.frombytes(cmsg_data[: struct.calcsize("i")])
            if fds:
                fd = int(fds[0])
                break
    if fd is None:
        raise RuntimeError("Layer response did not include a frame fd.")

    if len(msg) < 24:
        os.close(fd)
        raise RuntimeError(f"Frame header too short: {len(msg)}")

    width, height, stride, size, frame_id, frame_format = struct.unpack(
        "IIIIII", msg[:24]
    )
    with driver.dlpack_from_file_descriptor(fd, size, height, stride) as frame:
        yield cast(
            DolphinFrameBuffer,
            DolphinFrameBufferHandle(
                frame,
                width=width,
                height=height,
                stride=stride,
                size=size,
                frame_id=frame_id,
                frame_format=frame_format,
            ),
        )


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
        _frame_stack: ExitStack | None = None

        def execute(self, action: DolphinAction) -> None:
            _LOGGER.debug("execute called with action=%s (noop)", action)
            _packed_action = self._pack_action(action)
            self.control_socket.sendall(b"E" + _packed_action)
            if self.control_socket.recv(1) != b"D":
                raise RuntimeError(
                    "Dolphin session control socket disconnected unexpectedly during action execution"
                )
            return None

        @abstractmethod
        def memory_view(self) -> DolphinMemoryView: ...
        @abstractmethod
        def frame_buffer(self) -> DolphinFrameBuffer: ...
        @property
        @abstractmethod
        def control_socket(self) -> socket.socket: ...

        def _replace_frame_buffer(
            self, frame_buffer: AbstractContextManager[DolphinFrameBuffer]
        ) -> DolphinFrameBuffer:
            self.close_frame_buffer()
            self._frame_stack = ExitStack()
            return self._frame_stack.enter_context(frame_buffer)

        def close_frame_buffer(self) -> None:
            if self._frame_stack is None:
                return
            self._frame_stack.close()
            self._frame_stack = None

        @staticmethod
        def _pack_action(actions: DolphinAction) -> bytes:
            num_agents = len(actions)
            format_string = "<B" + ("H6f" * len(actions))

            data: list[int | float] = [num_agents]
            for agent_idx, action in actions:
                button_mask = 0
                for j, btn in enumerate(
                    [
                        "A",
                        "B",
                        "X",
                        "Y",
                        "Z",
                        "Start",
                        "Up",
                        "Down",
                        "Left",
                        "Right",
                        "L",
                        "R",
                    ]
                ):
                    if getattr(action, btn):
                        button_mask |= 1 << j
                agent_header = (((agent_idx - 1) & 0b11) << 12) | (button_mask & 0xFFF)
                data.append(agent_header)
                data.extend(
                    [
                        action.StickX,
                        action.StickY,
                        action.CStickX,
                        action.CStickY,
                        action.TriggerLeft,
                        action.TriggerRight,
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
            _LOGGER.debug("Created DolphinEnvironment.Session")

        def _refresh_frame_buffer(self) -> DolphinFrameBuffer:
            _LOGGER.debug("Refreshing frame buffer")
            return self._dolphin_scenario.dolphin.frame_buffer()

        def reset(self) -> tuple[tuple[DolphinMemoryView, DolphinFrameBuffer], None]:
            _LOGGER.info("Resetting Dolphin environment session")
            for _ in range(_INITIAL_FRAME_ATTEMPTS):
                self._dolphin_scenario.dolphin.execute(DolphinAction())
                try:
                    frame_buffer = self._refresh_frame_buffer()
                    break
                except DolphinFrameBufferUnavailable:
                    continue
            else:
                raise TimeoutError(
                    "Timed out waiting for initial Dolphin frame buffer."
                )
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
                scenario_session.dolphin.close_frame_buffer()


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
