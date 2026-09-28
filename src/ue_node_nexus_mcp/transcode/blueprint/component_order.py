"""Order authored component parents before their children."""

from graphlib import CycleError, TopologicalSorter


def ordered_components(declarations, plan):
    graph = dict((name, (decl.keyed()["parent"],) if decl.keyed().get("parent") in declarations else ())
                 for name, decl in declarations.items())
    try:
        return [(name, declarations[name]) for name in TopologicalSorter(graph).static_order()]
    except CycleError as exc:
        plan.error("component_cycle", "component parent cycle: " + " -> ".join(exc.args[1]))
        return []
