from contextlib import contextmanager
from types import TracebackType
from typing import Iterator, Self

from wii_arena.core.environment.types import Terminated, Truncated
from wii_arena.core.provision.protocols import Provision
from wii_arena.dolphin import Dolphin, DolphinScenario, DolphinScenarioAuthor


class MarioKartWiiGrandPrixScenarioAuthor(DolphinScenarioAuthor):
    def __init__(self, dolphin_launcher: Provision[Dolphin]):
        self._dolphin_launcher: Provision[Dolphin] = dolphin_launcher

    @contextmanager
    def session(self) -> Iterator[DolphinScenario]:
        with self._dolphin_launcher.session() as dolphin:
            # TODO: e.g. navigating the Dolphin emulator to the Grand Prix mode in Mario Kart Wii.
            yield MarioKartWiiGrandPrixScenario(dolphin=dolphin)
            # TODO: e.g. cleaning up after the Grand Prix scenario in Mario Kart Wii.


class MarioKartWiiGrandPrixScenario(DolphinScenario):
    def __enter__(self) -> Self:
        # TODO: implement this method to navigate the Dolphin emulator to the Grand Prix mode in Mario Kart Wii.
        ...

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ):
        # TODO: implement this method to clean up after the Grand Prix scenario in Mario Kart Wii.
        ...

    def terminated(self) -> Terminated:
        # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has terminated, e.g. by checking all the players have finished the race
        ...

    def truncated(self) -> Truncated:
        # TODO: implement this method to determine if the Grand Prix scenario in Mario Kart Wii has been truncated
        ...
