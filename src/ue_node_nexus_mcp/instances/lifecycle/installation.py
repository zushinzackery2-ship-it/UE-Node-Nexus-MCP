"""Verify the engine and all early-guard binaries before reserving a launch."""

import json
import mmap
from pathlib import Path
import re

from ...build_info.requirements import CONTRACT_VERSION, VERSION, CORE_MODULE, GUARD_MODULE
from ..errors import InstanceError, require


def read(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        require(isinstance(value, dict), "instance_incompatible", "installation metadata must be an object", path=str(path))
        return value
    except (OSError, ValueError) as exc:
        raise InstanceError("instance_incompatible", "installation metadata is missing or unreadable", dict(path=str(path))) from exc


def verify(project: dict, engine: Path) -> None:
    descriptor = read(Path(project["project_path"]))
    require(not any(item.get("Name") == CORE_MODULE and item.get("Enabled") is False for item in descriptor.get("Plugins", [])),
            "plugin_disabled", "enable UeNodeNexusBridge in this project before starting")
    roots = [Path(project["project_path"]).parent / "Plugins" / CORE_MODULE, engine / "Engine/Plugins/Editor" / CORE_MODULE]
    root = next((path for path in roots if (path / (CORE_MODULE + ".uplugin")).is_file()), None)
    require(root is not None, "plugin_missing", "install the matching UeNodeNexusBridge and Guard before starting")
    metadata = read(root / (CORE_MODULE + ".uplugin"))
    modules = dict((item["Name"], item) for item in metadata.get("Modules", []))
    require(metadata.get("VersionName") == VERSION and modules.get(GUARD_MODULE, dict()).get("LoadingPhase") == "PostConfigInit",
            "instance_incompatible", "install the matching bridge with its early lifecycle guard", required_version=VERSION)
    identity = read(root / "BuildIdentity.json")
    source_hash = identity.get("source_fingerprint", "")
    require(identity.get("contract_version") == CONTRACT_VERSION and identity.get("version") == VERSION
            and re.fullmatch(r"[a-fA-F0-9]{64}", source_hash), "instance_incompatible",
            "installed build identity differs from the MCP package", required_contract=CONTRACT_VERSION, actual=identity)
    binary_root = root / "Binaries/Win64"
    manifest = read(binary_root / "UnrealEditor.modules")
    expected = read(engine / "Engine/Binaries/Win64/UnrealEditor.modules").get("BuildId")
    require(expected and manifest.get("BuildId") == expected, "instance_incompatible", "plugin requires another engine build",
            expected_build_id=expected, actual_build_id=manifest.get("BuildId"))
    for name in (CORE_MODULE, GUARD_MODULE):
        binary = binary_root / manifest.get("Modules", dict()).get(name, "missing.dll")
        require(binary.is_file() and binary.resolve().is_relative_to(binary_root.resolve()),
                "instance_incompatible", "required editor module is missing", module=name, path=str(binary))
        try:
            with binary.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
                require(data[:2] == b"MZ" and data.find(source_hash.encode("utf-16le")) >= 0,
                        "instance_incompatible", "DLL does not match the installed source identity", module=name)
        except (OSError, ValueError) as exc:
            raise InstanceError("instance_incompatible", "required editor module could not be verified", dict(module=name)) from exc
