"""Rebuild MaterialFunctionCall nodes on the interface of the function they call.

A consumer published in the same batch rebuilds its call nodes inside its own
apply; refreshing it alone first compiles a graph that lacks the wiring the batch
is about to add, which is how a newly required input rolled back a caller whose
own apply would have connected it.
"""

from __future__ import annotations

from ..storage.paths import object_path
from ..diff.plan import AssetPlan, Verb
from ..text.model import Section
from .pins import is_function_call

REFRESH = "refresh_function_calls"
CONSUMER_KINDS = ("material", "material_function")
WIRING = ("connect_pins", "disconnect_pins")


def rebound_calls(local: Section, base: Section | None) -> frozenset[str]:
    """Call nodes kept by id whose ``MaterialFunction`` changes.

    Their ports belong to the new function, so the plan cuts every link they
    carry before the rebind and makes each one again after it, resolved by name
    on the new interface; an unchanged link text would otherwise keep the old
    function's output index.
    """
    if base is None:
        return frozenset()
    before = dict((decl.id, called_function(decl)) for decl in base.decls() if decl.modifier is None
                  and not decl.opaque and is_function_call(decl))
    return frozenset(decl.id for decl in local.decls() if decl.modifier is None and not decl.opaque
                     and is_function_call(decl) and decl.id in before and called_function(decl) != before[decl.id])


def called_function(decl) -> str | None:
    function = decl.keyed().get("MaterialFunction")
    return object_path(function) if function else None


def refresh_ops(functions) -> list[dict]:
    return [dict(op=REFRESH, function=function) for function in sorted(functions)]


def rebuild_position(ops, name=lambda op: op.get("op")) -> int:
    """After the plan cuts its old links, which may name ports the new interface
    dropped, and before it makes new ones, which may name ports only it has."""
    return next((index for index, op in enumerate(ops) if name(op) == "connect_pins"), len(ops))


def with_refresh(plan: list[dict], functions) -> list[dict]:
    """``plan`` with its call nodes of ``functions`` rebuilt inside the same apply,
    so that apply's single compile sees the final graph."""
    present = set(op.get("function") for op in plan if op.get("op") == REFRESH)
    position = rebuild_position(plan)
    return [*plan[:position], *refresh_ops(set(functions) - present), *plan[position:]]


def rebuild_wired_calls(graph, plan: AssetPlan) -> None:
    """Rebuild every call node the plan wires before the wiring lands.

    Text resolves a call's ports through the function's current interface, while
    the editor's node keeps the ports of its last refresh; a refresh that was
    rolled back leaves it on the old ones, and the caller's own fix could then
    never connect. Rebuilding a node that is already current changes nothing.
    """
    calls = dict((decl.id, called_function(decl)) for decl in graph.decls()
                 if not decl.opaque and is_function_call(decl) and called_function(decl))
    functions = set(calls[verb.args[key]] for verb in plan.verbs if verb.op in WIRING
                    for key in ("from", "to") if verb.args.get(key) in calls)
    if functions:
        position = rebuild_position(plan.verbs, lambda verb: verb.op)
        plan.verbs[position:position] = [Verb(REFRESH, dict(function=function)) for function in sorted(functions)]


def changed_calls(kind: str, dependencies, interfaces: set[str]) -> set[str]:
    """Functions this consumer calls whose new interface the publication already applied."""
    return set(dependencies) & interfaces if kind in CONSUMER_KINDS else set()
