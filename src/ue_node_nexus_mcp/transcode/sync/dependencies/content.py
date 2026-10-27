"""Content edges: consumers that apply against what their dependency publishes.

Availability lets an existing dependency skip the creation order, since most
consumers only need the object they name. A material-domain consumer needs more:
an instance checks every override against its parent's parameters, and a material
or function compiles the functions it calls, whose parameters become its own. A
batch has to apply these sources first even when their interfaces are unchanged;
otherwise the consumer applies against the content the batch is replacing.
"""

from __future__ import annotations

from ...material.calls import called_function
from ...material.pins import is_function_call
from ...text.model import Document
from .collect import dependency_kinds

MATERIAL_KINDS = frozenset(("material", "material_function", "material_instance"))


def direct_sources(kind: str, document: Document, schema=None) -> set[str]:
    """The parent of an instance; the functions a material or function calls."""
    if kind == "material_instance":
        return set(asset for asset, edges in dependency_kinds(document, kind, schema).items() if "superclass" in edges)
    graph = document.section("graph") if kind in MATERIAL_KINDS else None
    if graph is None:
        return set()
    return set(function for function in (called_function(decl) for decl in graph.decls() if is_function_call(decl)) if function)


def content_sources(documents: dict[str, tuple[str, Document]], resolve, schema=None) -> dict[str, set[str]]:
    """Each material-domain asset of ``documents`` -> every source it reads, transitively.

    ``resolve(asset)`` returns the ``(kind, document)`` the editor holds for a
    source outside ``documents``, or None when it is not mirrored. An unchanged
    source still passes on the sources behind it: an instance whose parent stays
    as it is reads the parameters of a function this batch changes. Every asset
    is resolved and parsed at most once per call.
    """
    known: dict[str, tuple[str, Document] | None] = dict(documents)
    direct: dict[str, set[str]] = dict()

    def sources(asset: str) -> set[str]:
        if asset not in direct:
            if asset not in known:
                known[asset] = resolve(asset)
            entry = known[asset]
            direct[asset] = direct_sources(entry[0], entry[1], schema) if entry is not None else set()
        return direct[asset]

    result: dict[str, set[str]] = dict()
    for asset, (kind, _) in documents.items():
        if kind not in MATERIAL_KINDS:
            continue
        seen: set[str] = set()
        pending = list(sources(asset))
        while pending:
            source = pending.pop()
            if source != asset and source not in seen:
                seen.add(source)
                pending.extend(sources(source))
        result[asset] = seen
    return result
