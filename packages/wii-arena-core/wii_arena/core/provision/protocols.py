from abc import ABC
from contextlib import AbstractContextManager


class Provision[T](ABC):
    def session(self) -> AbstractContextManager[T]: ...
