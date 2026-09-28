"""Validate the asset-wide namespace used by native Blueprint apply IDs."""

from ..errors import DiagnosticSink
from ..model import Document


GRAPH_SECTIONS = ("graph", "function", "macro")


def lint_node_ids(document: Document, sink: DiagnosticSink) -> None:
    seen = dict()
    for section in document.sections:
        if section.name not in GRAPH_SECTIONS:
            continue
        for decl in section.decls():
            if decl.modifier == "local":
                continue
            key = decl.id.casefold()
            previous = seen.get(key)
            if previous is not None:
                sink.error(
                    "duplicate_node_id",
                    f"node ID {decl.id!r} already occurs in {previous}; "
                    "node IDs must be unique ignoring case across all Blueprint graphs",
                    line=decl.line)
            else:
                seen[key] = f"{section.name} {section.args} (line {decl.line})"


def existing_node_ids(document: Document | None, known: dict[str, str]) -> dict[str, str]:
    """Only graph nodes participate in native node lookup; members use names."""
    result = dict()
    if document is None:
        return result
    for section in document.sections:
        if section.name not in GRAPH_SECTIONS:
            continue
        for decl in section.decls():
            if decl.modifier == "local":
                continue
            physical = decl.meta.get("guid") or known.get(decl.id)
            if physical:
                result[decl.id] = physical
    return result
