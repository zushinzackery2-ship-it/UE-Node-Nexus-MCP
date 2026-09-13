"""Stable semantic IDs with separate physical bindings and text aliases."""

from __future__ import annotations

from dataclasses import asdict, is_dataclass
from uuid import NAMESPACE_URL, uuid5

from ...model import Decl, Section
from ...sync_project import SyncError


def stable_id(key: str) -> str:
    return uuid5(NAMESPACE_URL, "nexus:" + key).hex


def plain(value):
    if is_dataclass(value):
        return plain(asdict(value))
    if isinstance(value, dict):
        return dict((key, plain(item)) for key, item in value.items())
    if isinstance(value, (tuple, list)):
        return [plain(item) for item in value]
    return value


def section_name(section: Section) -> str:
    args = section.args.split("(", 1)[0].strip() if section.name == "function" else section.args
    return f"{section.name}:{args}"


class Identities:
    def __init__(self, previous: dict | None, namespace: str, asset: str) -> None:
        self.previous = previous or dict()
        self.namespace = namespace
        self.asset = asset
        self.bindings = dict()
        self.sections = self.previous.get("semantic", dict()).get("sections", dict())
        self.old_bindings = self.previous.get("bindings", dict())
        self.by_alias = dict()
        self.by_physical = dict()
        self.section_aliases = dict()
        for key, section in self.sections.items():
            self.section_aliases[section_name(Section(section["name"], section["args"]))] = key
            for identifier, entity in section["entities"].items():
                self.by_alias[(key, entity["alias"])] = identifier
        for identifier, binding in self.old_bindings.items():
            if binding.get("physical"):
                self.by_physical[binding["physical"]] = identifier

    def scope(self, section: Section) -> str:
        name = section_name(section)
        return self.section_aliases.get(name, name)

    def entity(self, scope: str, decl: Decl) -> tuple[str, dict]:
        physical = str(decl.meta.get("physical") or decl.meta.get("guid") or "")
        alias_match = self.by_alias.get((scope, decl.id))
        if physical and self.old_bindings.get(alias_match, dict()).get("physical") not in (None, "", physical):
            alias_match = None
        identifier = decl.meta.get("semantic_id") or self.by_physical.get(physical) or alias_match
        if identifier is None:
            source = "physical:" + physical if physical else f"new:{self.namespace}:{scope}:{decl.id}"
            identifier = stable_id(self.asset + ":" + source)
        if identifier in self.bindings:
            raise SyncError("ambiguous_identity", f"duplicate entity identity {decl.id}")
        old = self.old_bindings.get(identifier, dict())
        metadata = dict(old.get("meta", dict()), **plain(decl.meta))
        binding = dict(physical=physical or old.get("physical", ""), scope=scope, alias=decl.id, meta=metadata)
        self.bindings[identifier] = binding
        return identifier, metadata


def scene_metadata(document, aliases: dict) -> None:
    actor_names = dict((alias, key.split("/")[1]) for key, alias in aliases.items() if key.startswith("actor/"))
    by_alias = dict()
    for key, alias in aliases.items():
        prefix, _, _ = key.rpartition("/")
        by_alias.setdefault((prefix + "/", alias), []).append(key)
    for section in document.sections:
        for decl in section.decls():
            prefix = "actor/"
            if section.name == "components":
                prefix = f"component/{actor_names.get(section.args, '')}/"
            elif section.name == "instances":
                actor, _, component = section.args.partition(".")
                owner = actor_names.get(actor, "")
                candidates = by_alias.get((f"component/{owner}/", component), [])
                component_id = candidates[0].rsplit("/", 1)[-1] if len(candidates) == 1 else ""
                prefix = f"instance/{owner}/{component_id}/"
            matches = by_alias.get((prefix, decl.id), [])
            if len(matches) == 1:
                decl.meta["physical"] = matches[0]
