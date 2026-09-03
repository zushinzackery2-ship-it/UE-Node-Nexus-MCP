"""Dependency ordering for multi-asset pushes (leaves first)."""

from __future__ import annotations

from .model import Document
from .paths import object_path


def document_dependencies(document: Document, kind: str) -> set[str]:
    """Object paths of /Game assets this document references structurally."""
    deps: set[str] = set()
    if kind in ("material", "material_function"):
        graph = document.section("graph")
        for decl in graph.decls() if graph else []:
            path = decl.keyed().get("MaterialFunction")
            if path:
                deps.add(object_path(path))
    elif kind == "material_instance":
        parent = _asset_prop(document, "Parent")
        if parent:
            deps.add(object_path(parent))
    elif kind == "niagara_system":
        for section in document.find_sections("emitter"):
            parent = section.prop_map().get("Parent")
            if parent is not None and parent.value:
                deps.add(object_path(parent.value))
    elif kind == "blueprint":
        parent = _asset_prop(document, "ParentClass")
        if parent and parent.startswith("/Game/"):
            deps.add(object_path(parent.removesuffix("_C")))
    return {dep for dep in deps if dep.startswith("/Game/")}


def _asset_prop(document: Document, key: str) -> str | None:
    section = document.section("asset")
    if section is None:
        return None
    prop = section.prop_map().get(key)
    return prop.value if prop else None


def order_assets(documents: dict[str, tuple[str, Document]]) -> list[str]:
    """Kahn ordering over the push set: dependencies before dependents."""
    pending = {asset: document_dependencies(document, kind) & set(documents) for asset, (kind, document) in documents.items()}
    ordered: list[str] = []
    while pending:
        ready = sorted(asset for asset, deps in pending.items() if not deps - set(ordered))
        if not ready:
            ordered.extend(sorted(pending))
            break
        for asset in ready:
            ordered.append(asset)
            pending.pop(asset)
    return ordered
