"""Catalog lookups of a lint: a spelling several records answer to fails its own line.

Raised out of a lint, the ambiguity ended the whole asset's check without a line,
and the binding a publish pins from the same lint went with it, so one shared
Blueprint class name could block an unrelated edit.
"""

from __future__ import annotations

from ..errors import DiagnosticSink
from ..errors import SyncError

# Returned after the ambiguity is reported; checks that need the one record skip.
AMBIGUOUS = object()


def lookup(sink: DiagnosticSink, line: int | None, code: str, subject: str, resolve, *args):
    """``resolve(*args)``, or AMBIGUOUS once ``subject`` is reported as ``code`` at ``line``."""
    try:
        return resolve(*args)
    except SyncError as exc:
        if exc.code != "schema_ambiguous":
            raise
        candidates = ", ".join(str(item) for item in exc.details.get("candidates", [])[:5])
        sink.error(code, f"{subject} names several records; write the full path: {candidates}", line=line)
        return AMBIGUOUS
