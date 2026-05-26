from contextlib import contextmanager
from typing import Iterator

from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import (
    Dolphin,
    DolphinScenario,
)


class MarioKartWiiGrandPrixScenario(DolphinScenario):
    class Session(DolphinScenario.Session):
        def terminated(self) -> Terminated:
            # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has terminated, e.g. by checking all the players have finished the race
            ...

        def truncated(self) -> Truncated:
            # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has been truncated
            ...

    def __init__(self, dolphin: Dolphin):
        self._dolphin = dolphin

    @contextmanager
    def session(self) -> Iterator[MarioKartWiiGrandPrixScenario.Session]:
        with self._dolphin.session() as dolphin_session:
            # TODO: e.g. navigating the Dolphin emulator to the Grand Prix mode in Mario Kart Wii.
            yield MarioKartWiiGrandPrixScenario.Session(dolphin_session=dolphin_session)
            # TODO: e.g. cleaning up after the Grand Prix scenario in Mario Kart Wii.
