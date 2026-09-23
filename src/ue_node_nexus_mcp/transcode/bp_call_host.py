"""Call nodes whose host class the called function decides.

The editor's function spawner picks ``K2Node_CallArrayFunction`` for a function
with ``ArrayParm`` metadata, ``K2Node_CallDataTableFunction`` for ``DataTablePin``
and ``K2Node_CallMaterialParameterCollectionFunction`` for collection functions;
only that class resolves their wildcard or context pins. The mirror therefore
names the call, ``CallFunction(Owner.Function)``, and the bridge picks the host.
A specialised spelling is accepted as the same call.
"""

from __future__ import annotations

CALL = "CallFunction"
HOSTS = frozenset(("CallFunction", "CallArrayFunction", "CallDataTableFunction", "CallMaterialParameterCollectionFunction"))


def host_name(text: str) -> str:
    """``/Script/BlueprintGraph.K2Node_CallArrayFunction`` -> ``CallArrayFunction``."""
    name = text.rsplit(".", 1)[-1]
    return name[len("K2Node_"):] if name.startswith("K2Node_") else name


def call_spelling(text: str) -> str:
    """The one spelling every host class of a function call shares."""
    return CALL if host_name(text) in HOSTS else text


def required_host(record: dict | None) -> str | None:
    """The host class a function record names, as a mirror spelling."""
    value = (record or dict()).get("node_class")
    return host_name(str(value)) if value else None
