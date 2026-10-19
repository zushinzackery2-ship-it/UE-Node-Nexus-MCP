"""Isolation is the caller's decision: off by default, auto for classified risk, always for every unit."""

from __future__ import annotations

MODES = ("off", "auto", "always")
DEFAULT = "off"


def isolation_mode(options: dict | None) -> str:
    """Return the requested mode; values are validated where options are parsed."""
    if not options:
        return DEFAULT
    mode = options.get("isolation", DEFAULT)
    return mode if mode in MODES else DEFAULT


def requires_reason(mode: str) -> bool:
    """Only ``auto`` filters candidates by the risk classification."""
    return mode == "auto"
