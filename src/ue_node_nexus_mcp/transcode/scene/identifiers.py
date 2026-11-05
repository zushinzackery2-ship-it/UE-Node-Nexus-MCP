"""Compare scene GUIDs by value while preserving labels, paths and attempt tokens."""

import re

from ..errors import SyncError


GUID = re.compile(r"[0-9a-fA-F]{32}")


def canonical_guid(value):
    return value.lower() if isinstance(value, str) and GUID.fullmatch(value) else value


def canonical_key(value: str) -> str:
    parts = value.split("/")
    if parts[0] not in ("actor", "component", "instance"):
        return value
    return "/".join([parts[0], *(canonical_guid(part) for part in parts[1:])])


def canonical_aliases(values: dict) -> dict:
    aliases = {}
    for key, alias in values.items():
        normalized = canonical_key(key)
        if normalized in aliases and aliases[normalized] != alias:
            raise SyncError("ambiguous_scene_identity", "one GUID is bound to different scene aliases",
                            dict(identity=normalized))
        aliases[normalized] = alias
    return aliases


def canonical_scene(raw: dict) -> dict:
    result = dict(raw)
    for field in ("id", "parent", "actor_id"):
        if field in raw:
            result[field] = canonical_guid(raw[field])
    for collection in ("actors", "components", "instances", "ops"):
        if collection in raw:
            result[collection] = [canonical_scene(row) for row in raw[collection]]
    if "instance_data" in raw:
        result["instance_data"] = canonical_scene(raw["instance_data"])
    if "aliases" in raw:
        result["aliases"] = canonical_aliases(raw["aliases"])
    return result
