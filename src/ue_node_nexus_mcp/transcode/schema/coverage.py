"""What a schema answer covers, and the exact call that supplies the rest.

The catalog records a coverage *state* per area. A bare state such as
``context_required`` tells the caller that something is missing without telling
them how to get it, which leaves reading an existing mirror or guessing as the
only way forward. These helpers turn a recorded state into an answer that names
the call, so the resolution travels with the gap it describes.
"""

from __future__ import annotations

from typing import Any

CONTEXT_REQUIRED = "context_required"

# A Blueprint node's pins are not a property of its class: the cast result pin is
# named after the target class, a call's pins come from the called function's
# signature, a macro instance's pins come from the macro's tunnels. Reflection
# over the node class cannot produce them; exporting one asset that uses the node
# can, and that is what schema(target=...) does.
BLUEPRINT_PINS_REASON = "a Blueprint node's pins come from the owning asset, not from the node class"
BLUEPRINT_PINS_RETURNS = "context.definition.blueprint.graphs[].nodes[].pins[] (name, dir, type, default, linked)"


def target_resolution(target: str | None = None) -> dict[str, Any]:
    """The schema call that binds a target asset and returns its real pins."""
    options: dict[str, Any] = dict(target=target or "<Blueprint asset path, e.g. /Game/Path/BP_Thing.BP_Thing>")
    return dict(
        action="schema",
        options=options,
        returns=BLUEPRINT_PINS_RETURNS,
        note="mirror pin names are language independent; a cast result pin is written as AsResult",
    )


def blueprint_pins(target: str | None = None) -> dict[str, Any]:
    return dict(state=CONTEXT_REQUIRED, reason=BLUEPRINT_PINS_REASON, resolve_with=target_resolution(target))


def expand(recorded: dict[str, Any]) -> dict[str, Any]:
    """Answer-time coverage: recorded states, with the resolvable ones explained.

    Expanding on read rather than on collection means a catalog published by an
    older build answers with the resolution too, without a refresh.
    """
    coverage = dict(recorded)
    if coverage.get("blueprint_pins") == CONTEXT_REQUIRED:
        coverage["blueprint_pins"] = blueprint_pins()
    return coverage
