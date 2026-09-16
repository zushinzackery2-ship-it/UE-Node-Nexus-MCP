"""Structured errors shared by the broker, session adapter and public tools."""

from ..errors import BridgeError


class InstanceError(BridgeError):
    def __init__(self, code: str, message: str, details: dict | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.details = dict(details or dict())

    def envelope(self) -> dict:
        return dict(ok=False, error=dict(code=self.code, message=str(self), details=self.details))


def require(condition: bool, code: str, message: str, **details) -> None:
    if not condition:
        raise InstanceError(code, message, details)
