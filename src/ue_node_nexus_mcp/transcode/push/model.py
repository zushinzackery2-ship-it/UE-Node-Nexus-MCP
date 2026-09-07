from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from ..errors import Diagnostic
from ..model import Document
from ..plan import AssetPlan
from ..sync_status import AssetStatus


@dataclass
class PushOptions:
    dry_run: bool = True
    compile: bool = True
    save: bool = True
    force: str | None = None
    allow_delete: bool = False
    stop_on_error: bool = True


@dataclass
class PushResult:
    rows: list[dict[str, Any]] = field(default_factory=list)
    plans: list[dict[str, Any]] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    normalized: list[str] = field(default_factory=list)
    refreshed: list[str] = field(default_factory=list)
    stopped: bool = False


@dataclass
class Prepared:
    status: AssetStatus
    document: Document
    plan: AssetPlan
    file: Path
    text: str
    base: dict[str, Any] | None = None
    reconcile: bool = False


def row(asset: str, kind: str, state: str, action: str, **details: Any) -> dict[str, Any]:
    return dict(asset=asset, kind=kind, state=state, action=action, **details)
