"""Load .nexusignore once per scan; built-in metadata exclusions are reserved."""

from __future__ import annotations

from dataclasses import dataclass
import logging
from pathlib import Path

from ...errors import SyncError
from ..io import confined
from .rules import Rule

LOGGER = logging.getLogger("ue_nexus.discovery")
IGNORE_FILE = ".nexusignore"


def metadata(parts: tuple[str, ...]) -> bool:
    return any(part.startswith(".") or part == "__pycache__" for part in parts)


@dataclass(frozen=True)
class IgnorePolicy:
    rules: tuple[Rule, ...] = ()

    @classmethod
    def load(cls, root: Path) -> IgnorePolicy:
        path = confined(root, IGNORE_FILE)
        try:
            text = path.read_text(encoding="utf-8-sig")
        except FileNotFoundError:
            return cls()
        except (OSError, UnicodeError) as exc:
            raise SyncError("invalid_ignore_file", f"cannot read {path}: {exc}", dict(file=str(path))) from exc
        rules = []
        for number, line in enumerate(text.splitlines(), 1):
            try:
                rule = Rule.parse(line)
            except ValueError as exc:
                raise SyncError("invalid_ignore_file", f"{path}:{number}: {exc}", dict(file=str(path), line=number)) from exc
            if rule is not None:
                rules.append(rule)
        LOGGER.debug("loaded ignore policy file=%s rules=%d", path, len(rules))
        return cls(tuple(rules))

    def excluded(self, parts: tuple[str, ...], directory: bool = False) -> bool:
        """Evaluate one entry whose parents the walker has already accepted."""
        if metadata(parts):
            return True
        excluded = False
        for rule in self.rules:
            if rule.matches(parts, directory):
                excluded = not rule.include
        return excluded

    def ignored(self, parts: tuple[str, ...], directory: bool = False) -> bool:
        """A re-included file still requires all of its parents to be included."""
        return any(self.excluded(parts[:end], directory or end < len(parts)) for end in range(1, len(parts) + 1))
