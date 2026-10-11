"""Compile root-relative ignore globs without matching across path separators."""

from __future__ import annotations

from dataclasses import dataclass
from fnmatch import translate
import re


@dataclass(frozen=True)
class Rule:
    segments: tuple[re.Pattern[str] | None, ...]
    basename: bool
    directory_only: bool
    include: bool

    @classmethod
    def parse(cls, line: str) -> Rule | None:
        value = line.rstrip()
        if not value or value.startswith("#"):
            return None
        include = value.startswith("!")
        if include:
            value = value[1:]
        elif value.startswith(("\\#", "\\!")):
            value = value[1:]
        directory_only = value.endswith("/")
        anchored = value.startswith("/")
        value = value.strip("/")
        if not value:
            raise ValueError("ignore pattern requires a file or directory name")
        basename = not anchored and "/" not in value
        segments = tuple(None if part == "**" else re.compile(translate(part)) for part in value.split("/"))
        return cls(segments, basename, directory_only, include)

    def matches(self, parts: tuple[str, ...], directory: bool) -> bool:
        if not parts or (self.directory_only and not directory):
            return False
        if self.basename:
            pattern = self.segments[0]
            return pattern is None or pattern.fullmatch(parts[-1]) is not None
        positions = set([0])
        for pattern in self.segments:
            if pattern is None:
                positions = set(range(min(positions), len(parts) + 1))
            else:
                positions = set(index + 1 for index in positions
                                if index < len(parts) and pattern.fullmatch(parts[index]) is not None)
            if not positions:
                return False
        return len(parts) in positions
