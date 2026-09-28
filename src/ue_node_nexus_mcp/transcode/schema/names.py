"""Ranked spellings over one catalog family, built once per published manifest.

A record answers to its key and path, to every trailing dotted part of its path
(``Actor``, ``KismetMathLibrary.Multiply_DoubleDouble``), to its real class name
and to the prefix-stripped aliases the mirror writes for node ids (``Constant``
for ``MaterialExpressionConstant``). A spelling is ambiguous only when two
records answer to it at the strongest tier that matches at all: ``Actor`` is the
real name of ``/Script/Engine.Actor`` and merely the stripped alias of
``/Script/Niagara.NiagaraActor``, so it names the first.
"""

from __future__ import annotations

from ..errors import SyncError

# Reflected records (functions, modules, material functions): a dotted suffix is
# how an owner-qualified member is written, before any real name.
RECORD_TIERS = ("key", "path", "suffix", "name", "alias")
CLASS_TIERS = ("key", "path", "name", "alias")
# A function owner is written from a native class path, never as a stripped node
# alias; answering ``Component`` with NiagaraComponent would call the wrong class.
OWNER_TIERS = ("key", "path", "name")
PREFIXES = {
    "material_expression": ("MaterialExpression",),
    "k2node": ("K2Node_", "EdGraphNode_", "AnimGraphNode_"),
    "niagara_renderer": ("Niagara",),
}
SUFFIXES = {"niagara_renderer": ("RendererProperties",)}
# ``CallFunction`` names both ``K2Node_CallFunction`` and ``AnimGraphNode_CallFunction``
# once the engine prefixes are stripped, so the plain spelling is pinned to the Blueprint
# node; AnimGraph nodes keep their own ``AnimGraphNode_CallFunction`` spelling.
K2_ALIASES = {
    "Branch": "K2Node_IfThenElse",
    "Sequence": "K2Node_ExecutionSequence",
    "Comment": "EdGraphNode_Comment",
    "Reroute": "K2Node_Knot",
    "CallFunction": "K2Node_CallFunction",
}


def simple_name(path: str) -> str:
    return path.rsplit(".", 1)[-1].rsplit("/", 1)[-1] if path else ""


def class_spellings(family: str, name: str) -> list[str]:
    """``name`` and the engine-prefixed forms a class of ``family`` may carry."""
    written = K2_ALIASES.get(name, name) if family == "k2node" else name
    spellings = [written]
    spellings.extend(f"{prefix}{written}{suffix}" for prefix in PREFIXES.get(family, ("",))
                     for suffix in SUFFIXES.get(family, ("",)))
    return list(dict.fromkeys(spellings))


class NameIndex:
    def __init__(self, family: str, entries: dict) -> None:
        self.family = family
        self.entries = entries
        self.tiers = dict((tier, dict()) for tier in ("path", "suffix", "name", "alias"))
        for key, entry in entries.items():
            path = str(entry.get("path") or key)
            real = simple_name(path)
            self.add("path", path, key)
            parts = path.split(".")
            for start in range(1, len(parts)):
                self.add("suffix", ".".join(parts[start:]), key)
            self.add("name", real, key)
            for alias in entry.get("aliases") or ():
                if alias not in (key, path, real):
                    self.add("alias", alias, key)

    def add(self, tier: str, spelling: str, key: str) -> None:
        if not spelling:
            return
        bucket = self.tiers[tier].setdefault(spelling, [])
        if key not in bucket:
            bucket.append(key)

    def matches(self, spellings: list[str], tier: str) -> list[str]:
        if tier == "key":
            return list(dict.fromkeys(spelling for spelling in spellings if spelling in self.entries))
        found = []
        for spelling in spellings:
            found.extend(key for key in self.tiers[tier].get(spelling, ()) if key not in found)
        return found

    def find(self, spellings: list[str], tiers: tuple[str, ...]) -> str | None:
        """The key of the one record ``spellings`` name at the strongest matching tier."""
        for tier in tiers:
            keys = self.matches(spellings, tier)
            if len(keys) > 1:
                keys = self.current(keys)
            if len(keys) > 1:
                raise SyncError("schema_ambiguous", f"{spellings[0]!r} names several {self.family} records; use the full UE path",
                                dict(family=self.family, name=spellings[0], tier=tier,
                                     candidates=[str(self.entries[key].get("path") or key) for key in keys]))
            if keys:
                return keys[0]
        return None

    def current(self, keys: list[str]) -> list[str]:
        """Engine libraries ship a superseded script beside the current one under one
        short name; only an unambiguous current record may answer to that spelling."""
        live = [key for key in keys if not self.entries[key].get("deprecated")]
        return live if len(live) == 1 else keys
