"""Dependencies from parsed document values, ordered before their consumers."""

from __future__ import annotations

import heapq
import re
from collections import defaultdict

from ..text.lexer import parse_kv_list
from ..blueprint.signature import parse_signature
from ..blueprint.types import join_type_text, parse_type_text
from ..text.model import Document
from ..storage.paths import object_path
from ..errors import SyncError
from ..text.values import is_balanced_group, unquote

_REFERENCE = re.compile(r"(?:[\w./]+')?(/Game/[^\s\"'(),{}\[\]<>:=]+)(?::([^\s\"'(),{}\[\]<>]+))?'?")


def _value_dependencies(value: str | None, owner: str) -> set[str]:
    if not value:
        return set()
    value = unquote(value.strip())
    match = _REFERENCE.fullmatch(value)
    if match:
        reference = match.group(1)
        asset = object_path(reference)
        # Serialized node connections reference subobjects owned by this asset.
        if match.group(2) and asset == owner:
            return set()
        # A Blueprint's generated class method is a member of the owning asset,
        # not a publication dependency on a second package.
        if asset == owner and reference.startswith(owner + "_C."):
            return set()
        return set((asset,))
    if is_balanced_group(value):
        return set().union(*(_value_dependencies(item, owner) for _, item in parse_kv_list(value[1:-1])))
    for wrapper in ("Object", "Class", "SoftObject", "SoftClass"):
        if value.startswith(wrapper + "(") and value.endswith(")"):
            return _value_dependencies(value[len(wrapper) + 1:-1], owner)
    return set()


def _type_dependencies(text: str, owner: str) -> set[str]:
    parsed = parse_type_text(text)
    return set().union(*(_value_dependencies(parsed.get(key), owner) for key in ("subobject", "value_subobject")))


def document_dependencies(document: Document, kind: str) -> set[str]:
    """Inspect AST values, including UE import-text structs and object arrays."""
    dependencies: set[str] = set()
    owner = object_path(document.header.asset)
    for section in document.sections:
        if section.name == "interfaces":
            for entry in section.bares():
                dependencies.update(_value_dependencies(entry.text, owner))
        signatures = [section.args] if section.name == "function" else []
        if section.name == "dispatchers":
            signatures.extend(entry.text for entry in section.bares())
        for text in signatures:
            signature = parse_signature(text)
            for parameter in (*signature.inputs, *signature.outputs):
                dependencies.update(_type_dependencies(parameter.type_text, owner))
                dependencies.update(_value_dependencies(parameter.default, owner))
        for prop in section.props():
            dependencies.update(_value_dependencies(prop.value, owner))
        for decl in section.decls():
            if kind == "blueprint" and (section.name == "variables" or decl.modifier == "local"):
                dependencies.update(_type_dependencies(join_type_text(decl.type_name, decl.args), owner))
            values = [decl.default, decl.type_name]
            values.extend(value for _, value in decl.args)
            values.extend(value for _, value in decl.props)
            for value in values:
                dependencies.update(_value_dependencies(value, owner))
    return dependencies - set((owner,)) if kind in ("blueprint", "asset", "scene") else dependencies


def order_assets(documents: dict[str, tuple[str, Document]], required: set[str] | None = None) -> list[str]:
    """Kahn ordering in O((V + E) log V); reject cycles before any writes.

    ``required`` restricts edges to assets whose new contents are needed by
    consumers. Existing mutually referencing Blueprints need no creation order.
    """
    selected = set(documents)
    required = selected if required is None else required
    dependencies = dict(
        (asset, document_dependencies(document, kind) & selected & required)
        for asset, (kind, document) in documents.items()
    )
    consumers: dict[str, list[str]] = defaultdict(list)
    indegrees = dict((asset, len(deps)) for asset, deps in dependencies.items())
    for asset, deps in dependencies.items():
        for dependency in deps:
            consumers[dependency].append(asset)
    ready = [asset for asset, count in indegrees.items() if count == 0]
    heapq.heapify(ready)
    ordered: list[str] = []
    while ready:
        asset = heapq.heappop(ready)
        ordered.append(asset)
        for consumer in consumers[asset]:
            indegrees[consumer] -= 1
            if indegrees[consumer] == 0:
                heapq.heappush(ready, consumer)
    blocked = sorted(asset for asset, count in indegrees.items() if count)
    if blocked:
        raise SyncError(
            "dependency_cycle", "cyclic asset dependencies block: " + ", ".join(blocked),
            dict(assets=blocked, dependencies=dict((asset, sorted(dependencies[asset])) for asset in blocked)),
        )
    return ordered
