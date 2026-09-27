"""Attach locations in rendered mirror text without discarding native metadata."""

from ...emitter import emit
from ...parser import parse
from ...sync_project import SyncError


def stamp(document) -> None:
    rendered, diagnostics = parse(emit(document))
    if diagnostics.has_errors:
        raise SyncError("invalid_document", "canonical mirror cannot be parsed",
                        dict(diagnostics=[item.format() for item in diagnostics.errors()]))
    for section, located in zip(document.sections, rendered.sections):
        section.line = located.line
        for entry, source in zip(section.entries, located.entries):
            entry.line = source.line
