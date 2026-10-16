"""Kahn asset ordering, available-edge pruning and cycle diagnostics."""

from __future__ import annotations

import heapq
from collections import defaultdict

from ...errors import SyncError
from ...text.model import Document
from .collect import dependency_kinds, document_dependencies

# Only these two edge classes can be satisfied by publishing declaration shells
# (asset, variables and signatures) before the full graphs.
_DECLARATION_KINDS = frozenset(("type", "signature"))


def _raise_cycle(documents, dependencies, blocked, schema) -> None:
    blocked_set = set(blocked)
    edge_kinds: set[str] = set()
    for asset in blocked:
        kind, document = documents[asset]
        kinds = dependency_kinds(document, kind, schema)
        for dependency in dependencies[asset]:
            if dependency in blocked_set:
                edge_kinds.update(kinds.get(dependency, ()))
    details = dict(assets=blocked, dependencies=dict((asset, sorted(dependencies[asset])) for asset in blocked))
    if edge_kinds and edge_kinds <= _DECLARATION_KINDS:
        raise SyncError(
            "declaration_cycle",
            "cyclic declaration dependencies block: " + ", ".join(blocked)
            + "; publish the class shells (asset, variables and signatures without graphs) of these assets first, then republish",
            details,
        )
    raise SyncError("dependency_cycle", "cyclic asset dependencies block: " + ", ".join(blocked), details)


def order_assets(documents: dict[str, tuple[str, Document]], required: set[str] | None = None,
                 available: set[str] | None = None, schema=None) -> list[str]:
    """Kahn ordering in O((V + E) log V); reject cycles before any writes.

    ``required`` restricts edges to assets whose new contents are needed by
    consumers. A dependency that is already ``available`` in the editor keeps
    its own content, so it constrains no creation order and its edges are
    dropped; an interface change simply keeps the asset out of ``available``.
    """
    selected = set(documents)
    required = selected if required is None else required
    available = set() if available is None else set(available)
    dependencies = dict(
        (asset, document_dependencies(document, kind, schema) & selected & required - available)
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
        _raise_cycle(documents, dependencies, blocked, schema)
    return ordered
