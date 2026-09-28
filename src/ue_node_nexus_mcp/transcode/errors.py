from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


class SyncError(Exception):
    """A synchronization failure with structured source and operation context."""

    def __init__(self, code: str, message: str, details: dict[str, Any] | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.details = details or dict()

    def attribute(self, **context: Any) -> "SyncError":
        """Keep the raiser's evidence when adding caller context."""
        for key, value in context.items():
            if value not in (None, ""):
                self.details.setdefault(key, value)
        return self


@dataclass(frozen=True)
class Diagnostic:
    """One ``file:line:col`` message produced by parse, lint, diff or push."""

    severity: str
    code: str
    message: str
    file: str | None = None
    line: int | None = None
    col: int | None = None

    def format(self) -> str:
        location = ""
        if self.file:
            location = self.file
            if self.line is not None:
                location += f":{self.line}"
                if self.col is not None:
                    location += f":{self.col}"
            location += ": "
        return f"{location}{self.severity} {self.code}: {self.message}"

    def to_json(self) -> dict[str, Any]:
        return {
            "severity": self.severity,
            "code": self.code,
            "message": self.message,
            "file": self.file,
            "line": self.line,
            "col": self.col,
            "text": self.format(),
        }


def error(code: str, message: str, *, file: str | None = None, line: int | None = None, col: int | None = None) -> Diagnostic:
    return Diagnostic("error", code, message, file, line, col)


def warning(code: str, message: str, *, file: str | None = None, line: int | None = None, col: int | None = None) -> Diagnostic:
    return Diagnostic("warning", code, message, file, line, col)


@dataclass
class DiagnosticSink:
    """Collects diagnostics and answers whether any error was recorded."""

    items: list[Diagnostic] = field(default_factory=list)
    file: str | None = None

    def add(self, diagnostic: Diagnostic) -> None:
        if diagnostic.file is None and self.file is not None:
            diagnostic = Diagnostic(diagnostic.severity, diagnostic.code, diagnostic.message, self.file, diagnostic.line, diagnostic.col)
        self.items.append(diagnostic)

    def error(self, code: str, message: str, line: int | None = None, col: int | None = None) -> None:
        self.add(error(code, message, line=line, col=col))

    def warning(self, code: str, message: str, line: int | None = None, col: int | None = None) -> None:
        self.add(warning(code, message, line=line, col=col))

    @property
    def has_errors(self) -> bool:
        return any(item.severity == "error" for item in self.items)

    def errors(self) -> list[Diagnostic]:
        return [item for item in self.items if item.severity == "error"]

    def extend(self, other: DiagnosticSink) -> None:
        self.items.extend(other.items)
