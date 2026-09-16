"""Mirror the verified isolated host build into the engine plugin folders.

UAT's ``BuildPlugin`` cannot resolve the ``UeNodeNexusGuard`` module rules of a plugin that
lives inside an installed engine, and it rewrites the descriptor without
``EnabledByDefault``. The isolated host build already produces exactly the binaries the
release package verifies, so the engine install copies that result instead of rebuilding.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT))

from release.identity import CONTRACT_VERSION, fingerprint
from tests.compile_check.prepare_host import PLUGINS, sync_directory

BLOCKING_IMAGES = ("UnrealEditor.exe", "UnrealEditor-Cmd.exe", "UnrealEditor-Win64-DebugGame.exe")


def require_editor_closed() -> None:
    for image in BLOCKING_IMAGES:
        result = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {image}", "/NH"],
                                capture_output=True, text=True, check=True)
        if image.lower() in result.stdout.lower():
            raise RuntimeError(f"close {image} before writing the engine plugin folders")


def verify_plugin(source: Path, built: Path) -> None:
    name = built.name
    identity = json.loads((built / "BuildIdentity.json").read_text(encoding="utf-8"))
    expected = fingerprint(source)
    if identity.get("source_fingerprint") != expected or fingerprint(built) != expected:
        raise RuntimeError(f"host build differs from the repository sources: {name}")
    descriptor = json.loads((built / f"{name}.uplugin").read_text(encoding="utf-8"))
    if identity.get("version") != descriptor["VersionName"] or identity.get("contract_version") != CONTRACT_VERSION:
        raise RuntimeError(f"build identity contract mismatch: {name}")
    binary_directory = built / "Binaries/Win64"
    expected_modules = dict((item["Name"], f"UnrealEditor-{item['Name']}.dll") for item in descriptor["Modules"])
    for module, binary_name in expected_modules.items():
        content = (binary_directory / binary_name).read_bytes()
        if content[:2] != b"MZ" or expected.encode("utf-16le") not in content:
            raise RuntimeError(f"DLL does not embed the current source fingerprint: {module}")
    manifest = json.loads((binary_directory / "UnrealEditor.modules").read_text(encoding="utf-8"))
    if manifest.get("Modules") != expected_modules:
        raise RuntimeError(f"module manifest does not match the descriptor: {name}")


def install(host: Path, engine: Path) -> None:
    host = host.resolve()
    engine = engine.resolve()
    host.relative_to((ROOT / "build").resolve())
    if not (engine / "Engine/Binaries/Win64/UnrealEditor.exe").is_file():
        raise RuntimeError(f"engine directory does not contain UnrealEditor.exe: {engine}")
    require_editor_closed()
    editor = engine / "Engine/Plugins/Editor"
    for plugin in PLUGINS:
        source = ROOT / "Plugins" / plugin
        built = host / "Plugins" / plugin
        verify_plugin(source, built)
        target = editor / plugin
        for name in ("Binaries", "Source", "Config", "Resources"):
            if (built / name).is_dir():
                sync_directory(built / name, target / name, engine)
        for name in (f"{plugin}.uplugin", "BuildIdentity.json"):
            (target / name).write_bytes((built / name).read_bytes())
        print(f"installed {plugin} into {target}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / "build/validation")
    parser.add_argument("--engine-dir", type=Path, required=True)
    args = parser.parse_args()
    install(args.host, args.engine_dir)


if __name__ == "__main__":
    main()
