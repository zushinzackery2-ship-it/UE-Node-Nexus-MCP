"""Static parity check between the Python operation manifest and the C++ registries.

The Python side declares bridge operations in operations.json; the C++ side
registers handlers in three places (core list, AutoIndex dispatch, VFX plugin
module). This test parses the registration strings out of the C++ sources so a
rename or an unregistered operation fails in CI instead of at editor runtime.
"""

from __future__ import annotations

import re
from pathlib import Path

from ue_node_nexus_mcp.contracts import BRIDGE_OPERATIONS

ROOT = Path(__file__).resolve().parents[1]
CORE_NAMES_FILE = (
    ROOT / "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeOperationNames.cpp"
)
AUTO_INDEX_DISPATCH_FILE = (
    ROOT / "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Core/UeNodeNexusBridgeAutoIndexDispatch.cpp"
)
VFX_MODULE_FILE = (
    ROOT / "Plugins/UeNodeNexusVfxBridge/Source/UeNodeNexusVfxBridge/Private/Module/UeNodeNexusVfxBridgeModule.cpp"
)

_OPERATION_NAME_RE = re.compile(r'TEXT\("([a-z][a-z0-9_]*)"\)')
_VFX_REGISTRATION_RE = re.compile(r'\{\s*TEXT\("([a-z][a-z0-9_]*)"\)')


def _core_operations() -> set[str]:
    return set(_OPERATION_NAME_RE.findall(CORE_NAMES_FILE.read_text(encoding="utf-8")))


def _auto_index_operations() -> set[str]:
    names = _OPERATION_NAME_RE.findall(AUTO_INDEX_DISPATCH_FILE.read_text(encoding="utf-8"))
    return {name for name in names if name.startswith("auto_index_")}


def _vfx_operations() -> set[str]:
    return set(_VFX_REGISTRATION_RE.findall(VFX_MODULE_FILE.read_text(encoding="utf-8")))


def test_cpp_registries_match_python_bridge_operations() -> None:
    core = _core_operations()
    auto_index = _auto_index_operations()
    vfx = _vfx_operations()

    assert core, "failed to parse core operation names from C++"
    assert auto_index, "failed to parse AutoIndex operation names from C++"
    assert vfx, "failed to parse VFX operation names from C++"

    assert core.isdisjoint(auto_index)
    assert core.isdisjoint(vfx)
    assert auto_index.isdisjoint(vfx)

    cpp_operations = core | auto_index | vfx
    missing_in_cpp = sorted(BRIDGE_OPERATIONS - cpp_operations)
    missing_in_python = sorted(cpp_operations - BRIDGE_OPERATIONS)

    assert missing_in_cpp == [], f"declared in operations.json but not registered in C++: {missing_in_cpp}"
    assert missing_in_python == [], f"registered in C++ but missing from operations.json: {missing_in_python}"
