from abc import ABC
from contextlib import AbstractContextManager


class SupportsSession(ABC):
    class Session: ...

    def session(self) -> AbstractContextManager[Session]: ...
