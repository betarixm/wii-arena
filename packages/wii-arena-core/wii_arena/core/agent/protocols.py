from abc import ABC, abstractmethod


class Agent[Observation, Action](ABC):
    @abstractmethod
    def act(self, observation: Observation) -> Action: ...
