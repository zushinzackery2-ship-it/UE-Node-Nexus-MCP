"""Single-source-of-truth and C++ drift gate for the operation contract.

operations.json is the one place operation names live on the Python side;
contracts.py derives every set from it. The C++ bridge still hand-maintains its
own operation-name lists (each name is wired to a distinct handler), so these
tests parse those C++ lists and assert they match the manifest bucket-by-bucket.
A misplaced or missing operation fails here without needing a live UE editor.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from ue_node_nexus_mcp.contracts import (
    ALL_OPERATIONS,
    BRIDGE_OPERATIONS,
    DEFAULT_HIDDEN_OPERATIONS,
    LOCAL_MCP_OPERATIONS,
    OPERATION_FEATURES,
    READ_OPERATIONS,
    WRITE_OPERATIONS,
)

_MANIFEST = Path("src/ue_node_nexus_mcp/operations.json")

_CORE_NAMES_CPP = "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeOperationNames.cpp"
_AUTO_INDEX_CPP = "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeAutoIndexDispatch.cpp"
_NIAGARA_MODULE_CPP = "Plugins/UeNodeNexusNiagaraBridge/Source/UeNodeNexusNiagaraBridge/Private/Module/UeNodeNexusNiagaraBridgeModule.cpp"
_CORE_REGISTRY_CPP = "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeCoreOperationsRegistry.cpp"
_AUTO_INDEX_REGISTRY_CPP = "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeAutoIndexDispatch.cpp"


def _manifest_records() -> list[dict]:
    return json.loads(_MANIFEST.read_text(encoding="utf-8"))["operations"]


def _bridge_bucket(*, groups: set[str] | None = None, exclude_groups: set[str] | None = None) -> set[str]:
    names: set[str] = set()
    for record in _manifest_records():
        if record.get("local"):
            continue
        group = record["group"]
        if groups is not None and group not in groups:
            continue
        if exclude_groups is not None and group in exclude_groups:
            continue
        names.add(record["name"])
    return names


def _cpp_operations(path: str) -> set[str]:
    content = Path(path).read_text(encoding="utf-8")
    # Filter to known operations so unrelated TEXT() literals (e.g. ensure
    # messages, JSON field names) do not pollute the comparison.
    return set(re.findall(r'TEXT\("([^"]+)"\)', content)) & ALL_OPERATIONS


def test_contracts_are_derived_from_the_manifest() -> None:
    records = _manifest_records()
    assert {r["name"] for r in records if r["kind"] == "read"} == READ_OPERATIONS
    assert {r["name"] for r in records if r["kind"] == "write"} == WRITE_OPERATIONS
    assert {r["name"] for r in records if r.get("local")} == LOCAL_MCP_OPERATIONS
    assert {r["name"] for r in records if r.get("hidden")} == DEFAULT_HIDDEN_OPERATIONS
    assert {r["name"]: r["group"] for r in records} == OPERATION_FEATURES


def test_core_cpp_list_matches_manifest_core_bucket() -> None:
    core_bucket = _bridge_bucket(exclude_groups={"auto_index", "niagara"})
    assert _cpp_operations(_CORE_NAMES_CPP) == core_bucket


def test_auto_index_cpp_list_matches_manifest_auto_index_bucket() -> None:
    assert _cpp_operations(_AUTO_INDEX_CPP) == _bridge_bucket(groups={"auto_index"})


def test_niagara_module_registers_exactly_the_manifest_niagara_bucket() -> None:
    assert _cpp_operations(_NIAGARA_MODULE_CPP) == _bridge_bucket(groups={"niagara"})


def test_core_registry_wires_exactly_the_core_bucket() -> None:
    # The startup registration list must match the reported core names so
    # capabilities never advertise an operation that has no handler.
    core_bucket = _bridge_bucket(exclude_groups={"auto_index", "niagara"})
    assert _cpp_operations(_CORE_REGISTRY_CPP) == core_bucket


def test_auto_index_registry_wires_exactly_the_auto_index_bucket() -> None:
    assert _cpp_operations(_AUTO_INDEX_REGISTRY_CPP) == _bridge_bucket(groups={"auto_index"})


def test_all_cpp_buckets_union_to_bridge_operations() -> None:
    core = _cpp_operations(_CORE_NAMES_CPP)
    auto_index = _cpp_operations(_AUTO_INDEX_CPP)
    niagara = _cpp_operations(_NIAGARA_MODULE_CPP)
    assert core.isdisjoint(auto_index)
    assert core.isdisjoint(niagara)
    assert auto_index.isdisjoint(niagara)
    assert core | auto_index | niagara == BRIDGE_OPERATIONS
