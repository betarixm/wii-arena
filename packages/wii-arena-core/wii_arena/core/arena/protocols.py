from abc import ABC, abstractmethod
from typing import Iterator

from ..agent.protocols import Agent
from ..environment.protocols import Environment
from ..environment.types import Terminated, Truncated
from ..provision.protocols import Provision


class Arena[Event, Observation, Action, Context](ABC):
    def __init__(
        self,
        agents: list[Agent[Observation, Action]],
        universe: Provision[Environment[Observation, Action, Context]],
    ):
        self._agents = agents
        self._universe = universe

    def stream(self) -> Iterator[Event]:
        with self._universe.session() as environment:
            observation, context = environment.reset()
            terminated, truncated = Terminated(False), Truncated(False)

            yield self._event_from_observation(observation, context)

            while not (terminated or truncated):
                for agent in self._agents:
                    action = agent.act(observation)
                    observation, terminated, truncated, context = environment.step(
                        action
                    )
                    yield self._event_from_observation(observation, context)

    @abstractmethod
    def _event_from_observation(
        self, observation: Observation, context: Context
    ) -> Event: ...
