from abc import ABC, abstractmethod
from contextlib import AbstractContextManager


class Provision[T](ABC):
    @abstractmethod
    def session(self) -> AbstractContextManager[T]: ...
