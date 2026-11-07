"""Refresh existing callers whose candidate function changed its physical pins."""

from .pins import candidate_function, function_pins
from .signature import parse_signature
from .types import type_text


def contract(pins):
    return sorted((item.get("name", ""), item.get("dir", ""), type_text(item.get("type")))
                  for item in pins if item.get("name") != "self")


def refresh_calls(document, base, plan, schema=None):
    if base is None:
        return
    refresh = []
    for section in document.sections:
        if section.name not in ("graph", "function"):
            continue
        graph = parse_signature(section.args).name if section.name == "function" else section.args
        before = next((item for item in base.sections if item.name == section.name
                       and (parse_signature(item.args).name if item.name == "function" else item.args) == graph), None)
        old = before.decl_map() if before else {}
        for decl in section.decls():
            previous = old.get(decl.id)
            candidate = candidate_function(decl, document, schema)
            if previous is None or candidate is None or candidate.get("missing") or previous.positional() != decl.positional():
                continue
            physical = previous.meta.get("pins")
            if physical is not None and contract(physical) != contract(function_pins(candidate, physical)):
                refresh.append((decl.id, graph, decl.line))
    start = len(plan.verbs)
    for identifier, graph, line in refresh:
        plan.add("refresh_function_call", id=identifier, graph=graph, line=line)
    plan.verbs[:] = plan.verbs[start:] + plan.verbs[:start]
