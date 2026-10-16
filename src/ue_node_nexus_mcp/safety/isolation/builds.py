"""Resolve the exact binary that Unreal's module manifest selects."""

import json
from pathlib import Path

from ...instances.errors import require


def module_binary(directory: Path, module: str) -> Path:
    binaries = directory / "Binaries/Win64"
    manifest = binaries / "UnrealEditor.modules"
    require(manifest.is_file(), "isolation_identity_missing",
        "validation requires a module manifest", plugin=directory.name, module=module)
    data = json.loads(manifest.read_text(encoding="utf-8-sig"))
    name = data.get("Modules", dict()).get(module)
    require(isinstance(name, str) and name.endswith(".dll") and Path(name).name == name
        and "/" not in name and "\\" not in name,
        "isolation_identity_mismatch", "module manifest must select a local DLL",
        plugin=directory.name, module=module)
    binary = binaries / name
    require(binary.is_file(), "isolation_identity_missing",
        "selected module binary is absent", plugin=directory.name, module=module, binary=str(binary))
    require(binary.resolve().is_relative_to(binaries.resolve()), "isolation_identity_mismatch",
        "selected module binary escapes its plugin", plugin=directory.name, module=module)
    return binary
