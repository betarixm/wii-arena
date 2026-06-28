from abc import ABC, abstractmethod
from typing import Iterator, Mapping

from ..agent.protocols import Agent
from ..environment.protocols import Environment
from ..environment.types import Terminated, Truncated


class Arena[Event, Observation, AgentAction, Context, AgentId](ABC):
    # Every agent acts on the same observation; all actions are then applied
    # together in a single environment step (not one step per agent).
    def __init__(
        self,
        agents: Mapping[AgentId, Agent[Observation, AgentAction]],
        environment: Environment[Observation, dict[AgentId, AgentAction], Context],
    ):
        self._agents = agents
        self._environment = environment

    def stream(self) -> Iterator[Event]:
        with self._environment.session() as environment:
            observation, context = environment.reset()
            terminated, truncated = Terminated(False), Truncated(False)

            yield self._event_from_observation(observation, context)

            while not (terminated or truncated):
                actions = {
                    agent_id: agent.act(observation)
                    for agent_id, agent in self._agents.items()
                }
                observation, terminated, truncated, context = environment.step(actions)
                yield self._event_from_observation(observation, context)

    @abstractmethod
    def _event_from_observation(
        self, observation: Observation, context: Context
    ) -> Event: ...
