"""MCP-local workflow guide operation.

Serves curated task-level guides (the cross-operation knowledge that individual
payload schemas cannot express) through the normal ``ue_execute`` path, so an
agent can pull authoring recipes on demand instead of relying on an externally
installed skill file. Guide bodies live in ``guides/*.md``.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from .runtime import default_tool

_GUIDES_DIR = Path(__file__).with_name("guides")

# category -> (title, keywords). Keywords drive query matching; keep them
# lowercase. The markdown file name is "<category>.md".
GUIDE_INDEX: dict[str, tuple[str, tuple[str, ...]]] = {
    "getting_started": (
        "Thin facade basics: discover, execute, verify",
        ("facade", "schema", "response mode", "artifact", "dry run", "start", "workflow", "discover"),
    ),
    "graph_editing": (
        "Reading and patching Material/Blueprint graphs",
        ("graph", "node", "pin", "wire", "patch", "snapshot", "connect", "client_id", "build"),
    ),
    "material_authoring": (
        "Materials, instances, node params, lint, compile",
        ("material", "instance", "texture", "lint", "expression", "parameter", "compile", "shader"),
    ),
    "blueprint_authoring": (
        "Blueprint details, SCS components, event graphs, input",
        ("blueprint", "component", "scs", "event", "variable", "input", "mapping", "k2node", "anim"),
    ),
    "niagara_authoring": (
        "Niagara systems, emitters, module stacks, renderers",
        ("niagara", "vfx", "emitter", "module", "renderer", "particle", "cascade", "user param"),
    ),
    "diagnostics_repair": (
        "Diagnostics semantics, triage, and compile-fix loops",
        ("diagnostics", "error", "warning", "repair", "empty", "contract", "log", "triage", "fix"),
    ),
    "concurrency": (
        "Parallel-safe reads, per-asset writes, batching",
        ("concurrency", "parallel", "batch", "sequential", "queue", "conflict", "order", "timeout"),
    ),
}


def guide_categories() -> list[dict[str, Any]]:
    return [
        {"category": name, "title": title, "keywords": list(keywords)}
        for name, (title, keywords) in sorted(GUIDE_INDEX.items())
    ]


def load_guide(category: str) -> str:
    if category not in GUIDE_INDEX:
        known = ", ".join(sorted(GUIDE_INDEX))
        raise ValueError(f"unknown guide category: {category}; known categories: {known}")
    return (_GUIDES_DIR / f"{category}.md").read_text(encoding="utf-8")


def search_guides(query: str) -> list[dict[str, Any]]:
    """Rank categories by keyword/title hits in the query string."""
    normalized = query.strip().lower()
    query_tokens = set(normalized.replace(",", " ").replace(":", " ").split())
    scored: list[tuple[int, str, str]] = []
    for name, (title, keywords) in GUIDE_INDEX.items():
        score = sum(2 for keyword in keywords if keyword in normalized)
        score += sum(1 for word in title.lower().replace(",", " ").split() if len(word) > 3 and word in query_tokens)
        if name.replace("_", " ") in normalized:
            score += 3
        if score:
            scored.append((score, name, title))
    scored.sort(key=lambda item: (-item[0], item[1]))
    return [{"category": name, "title": title, "score": score} for score, name, title in scored]


@default_tool()
def workflow_guide_get(category: str | None = None, query: str | None = None) -> dict[str, Any]:
    """Return workflow guide categories, one guide body, or keyword matches."""
    if category is not None and query is not None:
        raise ValueError("provide at most one of category / query")

    if category is not None:
        if not isinstance(category, str) or not category.strip():
            raise ValueError("category must be a non-empty string")
        data: dict[str, Any] = {
            "category": category,
            "title": GUIDE_INDEX.get(category, ("", ()))[0],
            "body": load_guide(category),
        }
    elif query is not None:
        if not isinstance(query, str) or not query.strip():
            raise ValueError("query must be a non-empty string")
        matches = search_guides(query)
        data = {"query": query, "matches": matches}
        if matches:
            best = matches[0]["category"]
            data["best"] = {"category": best, "body": load_guide(best)}
        else:
            data["categories"] = guide_categories()
    else:
        data = {
            "categories": guide_categories(),
            "usage": {
                "get_one": {"category": "getting_started"},
                "search": {"query": "connect material pins"},
            },
        }

    return {
        "ok": True,
        "operation": "workflow_guide_get",
        "data": data,
        "diagnostics": [],
        "warnings": [],
    }
