from contextlib import contextmanager
import logging
from typing import Iterator

from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import (
    Dolphin,
    DolphinScenario,
)

_LOGGER = logging.getLogger(__name__)


class MarioKartWiiGrandPrixScenario(DolphinScenario):
    class Session(DolphinScenario.Session):
        def terminated(self) -> Terminated:
            _LOGGER.debug("Checking Grand Prix terminated state")
            # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has terminated, e.g. by checking all the players have finished the race
            return Terminated(False)

        def truncated(self) -> Truncated:
            _LOGGER.debug("Checking Grand Prix truncated state")
            # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has been truncated
            return Truncated(False)

    def __init__(self, dolphin: Dolphin):
        self._dolphin = dolphin
        _LOGGER.debug(
            "Initialized MarioKartWiiGrandPrixScenario with dolphin=%s",
            type(dolphin).__name__,
        )

    @contextmanager
    def session(self) -> Iterator[MarioKartWiiGrandPrixScenario.Session]:
        _LOGGER.info("Opening MarioKartWiiGrandPrixScenario session")
        with self._dolphin.session() as dolphin_session:
            # TODO: e.g. navigating the Dolphin emulator to the Grand Prix mode in Mario Kart Wii.
            scenario_session = MarioKartWiiGrandPrixScenario.Session(
                dolphin_session=dolphin_session
            )
            _LOGGER.debug("Mario Kart scenario session ready")
            yield scenario_session
            _LOGGER.info("Closing MarioKartWiiGrandPrixScenario session")
            # TODO: e.g. cleaning up after the Grand Prix scenario in Mario Kart Wii.
