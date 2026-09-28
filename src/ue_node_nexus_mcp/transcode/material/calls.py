"""Rebuild MaterialFunctionCall nodes on the interface of the function they call.

A consumer published in the same batch rebuilds its call nodes inside its own
apply; refreshing it alone first compiles a graph that lacks the wiring the batch
is about to add, which is how a newly required input rolled back a caller whose
own apply would have connected it.
"""

from __future__ import annotations

from ..storage.paths import object_path
from ..diff.plan import AssetPlan, Verb
from .pins import is_function_call

REFRESH = "refresh_function_calls"
CONSUMER_KINDS = ("material", "material_function")
WIRING = ("connect_pins", "disconnect_pins")


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
    calls = dict((decl.id, object_path(decl.keyed()["MaterialFunction"])) for decl in graph.decls()
                 if not decl.opaque and is_function_call(decl) and decl.keyed().get("MaterialFunction"))
    functions = set(calls[verb.args[key]] for verb in plan.verbs if verb.op in WIRING
                    for key in ("from", "to") if verb.args.get(key) in calls)
    if functions:
        position = rebuild_position(plan.verbs, lambda verb: verb.op)
        plan.verbs[position:position] = [Verb(REFRESH, dict(function=function)) for function in sorted(functions)]


def changed_calls(kind: str, dependencies, interfaces: set[str]) -> set[str]:
    """Functions this consumer calls whose new interface the publication already applied."""
    return set(dependencies) & interfaces if kind in CONSUMER_KINDS else set()
