"""Finalize module manifests omitted by UBT's restricted-module build mode."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from release.identity import fingerprint


def finalize(host: Path, engine: Path) -> None:
    host = host.resolve()
    host.relative_to((ROOT / "build").resolve())
    reference = json.loads((engine / "Engine/Binaries/Win64/UnrealEditor.modules").read_text(encoding="utf-8"))
    build_id = reference["BuildId"]
    for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge"):
        plugin = host / "Plugins" / name
        identity = json.loads((plugin / "BuildIdentity.json").read_text(encoding="utf-8"))
        expected = fingerprint(plugin)
        binary_name = f"UnrealEditor-{name}.dll"
        directory = plugin / "Binaries/Win64"
        binary = (directory / binary_name).read_bytes()
        if identity["source_fingerprint"] != expected or expected.encode("utf-16le") not in binary:
            raise RuntimeError(f"rebuild required before finalizing module metadata: {name}")
        data = dict(BuildId=build_id, Modules=dict(((name, binary_name),)))
        (directory / "UnrealEditor.modules").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (host / "Content").mkdir(exist_ok=True)
    print(f"Verified module manifests: {host}, BuildId={build_id}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--engine-dir", type=Path, required=True)
    args = parser.parse_args()
    finalize(args.host, args.engine_dir)
