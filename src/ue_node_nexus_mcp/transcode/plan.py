"""Plan IR: the verb list produced by diff and consumed by ``transcode_apply``."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .errors import Diagnostic

RISKY_VERBS = {"delete_node", "bp_variable_remove", "bp_component_remove", "bp_function_remove", "ns_emitter_remove", "ns_module_remove", "ns_renderer_remove"}


@dataclass
class Verb:
    op: str
    args: dict[str, Any] = field(default_factory=dict)
    line: int | None = None

    def to_payload(self) -> dict[str, Any]:
        payload: dict[str, Any] = {"op": self.op}
        payload.update({key: value for key, value in self.args.items() if value is not None or key == "value"})
        if self.line is not None:
            payload["line"] = self.line
        return payload


@dataclass
class AssetPlan:
    asset_path: str
    kind: str
    verbs: list[Verb] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    ids: dict[str, str] = field(default_factory=dict)
    creates_asset: bool = False
    asset_class: str = ""
    interface_changed: bool = False

    def add(self, op: str, line: int | None = None, **args: Any) -> Verb:
        verb = Verb(op=op, args=args, line=line)
        self.verbs.append(verb)
        return verb

    def warn(self, code: str, message: str, line: int | None = None) -> None:
        self.diagnostics.append(Diagnostic("warning", code, message, None, line))

    def error(self, code: str, message: str, line: int | None = None) -> None:
        self.diagnostics.append(Diagnostic("error", code, message, None, line))

    @property
    def has_errors(self) -> bool:
        return any(item.severity == "error" for item in self.diagnostics)

    @property
    def empty(self) -> bool:
        return not self.verbs and not self.creates_asset

    def counts(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for verb in self.verbs:
            counts[verb.op] = counts.get(verb.op, 0) + 1
        return dict(sorted(counts.items()))

    def risky(self) -> list[Verb]:
        return [verb for verb in self.verbs if verb.op in RISKY_VERBS]

    def to_payload(self) -> dict[str, Any]:
        return {
            "asset_path": self.asset_path,
            "kind": self.kind,
            "asset_class": self.asset_class,
            "create": self.creates_asset,
            "ids": dict(self.ids),
            "plan": [verb.to_payload() for verb in self.verbs],
        }

    def summary(self, limit: int = 8) -> dict[str, Any]:
        return {
            "asset": self.asset_path,
            "kind": self.kind,
            "create": self.creates_asset,
            "verbs": len(self.verbs),
            "counts": self.counts(),
            "risky": len(self.risky()),
            "interface_changed": self.interface_changed,
            "first": [verb.to_payload() for verb in self.verbs[:limit]],
            "warnings": [item.format() for item in self.diagnostics if item.severity == "warning"][:limit],
            "errors": [item.format() for item in self.diagnostics if item.severity == "error"][:limit],
        }
