from contextlib import contextmanager
import logging
from typing import Iterator

from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.dolphin import (
    Dolphin,
    DolphinScenario,
)

_LOGGER = logging.getLogger(__name__)


class MarioKartWiiScenario(DolphinScenario):
    class Session(DolphinScenario.Session):
        def terminated(self) -> Terminated:
            _LOGGER.debug("Checking Mario Kart Wii terminated state")
            # TODO: implement this method to determine if the Mario Kart Wii scenario has terminated, e.g. by checking all the players have finished the race
            return Terminated(False)

        def truncated(self) -> Truncated:
            _LOGGER.debug("Checking Mario Kart Wii truncated state")
            # TODO: implement this method to determine if the Mario Kart Wii scenario has been truncated
            return Truncated(False)

    def __init__(self, dolphin: Dolphin):
        self._dolphin = dolphin
        _LOGGER.debug(
            "Initialized MarioKartWiiScenario with dolphin=%s",
            type(dolphin).__name__,
        )

    @contextmanager
    def session(self) -> Iterator[MarioKartWiiScenario.Session]:
        _LOGGER.info("Opening MarioKartWiiScenario session")
        with self._dolphin.session() as dolphin_session:
            # TODO: e.g. navigating the Dolphin emulator into a race in Mario Kart Wii.
            scenario_session = MarioKartWiiScenario.Session(
                dolphin_session=dolphin_session
            )
            _LOGGER.debug("Mario Kart scenario session ready")
            yield scenario_session
            _LOGGER.info("Closing MarioKartWiiScenario session")
            # TODO: e.g. cleaning up after the Mario Kart Wii scenario.
