"""Copy a supplied reproduction project and verified plugins into a new host."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from release.identity import fingerprint
from tests.compile_check.prepare_host import sync_directory

ROOT = Path(__file__).resolve().parents[3]
PLUGINS = ("UeNodeNexusBridge", "UeNodeNexusVfxBridge")


def copy_directory(source: Path, target: Path) -> None:
    for file in source.rglob("*"):
        if file.is_symlink():
            raise ValueError(f"reproduction input contains a link: {file}")
    shutil.copytree(source, target, ignore=shutil.ignore_patterns("Intermediate", "Saved", "DerivedDataCache", ".vs", "*.pdb"))


def stage_plugins(build: Path, host: Path) -> None:
    for name in PLUGINS:
        source = build / "Plugins" / name
        identity = json.loads((source / "BuildIdentity.json").read_text(encoding="utf-8"))
        assert fingerprint(source) == identity["source_fingerprint"], name
        target = host / "Plugins" / name
        target.mkdir(parents=True)
        for folder in ("Source", "Config", "Resources", "Binaries"):
            copy_directory(source / folder, target / folder)
        for filename in (f"{name}.uplugin", "BuildIdentity.json"):
            shutil.copy2(source / filename, target / filename)


def stage_replay(source: Path, host: Path) -> dict:
    mirror = source / "Content_Transcoded" / source.name / "ForgePostProcess"
    files = sorted(mirror.rglob("*.nexus"))
    assert len(files) == 30, f"expected the captured 30-asset Forge mirror: {mirror}"
    inputs = host / "ReplayInputs/ForgePostProcess"
    records = []
    for file in files:
        relative = file.relative_to(mirror)
        target = inputs / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
        text = file.read_text(encoding="utf-8")
        asset = next(line.removeprefix("asset:").strip() for line in text.splitlines() if line.startswith("asset:"))
        package = asset.split(".", 1)[0]
        assert package.startswith("/Game/ForgePostProcess/"), package
        copied_asset = host / "Content" / (package.removeprefix("/Game/") + ".uasset")
        copied_asset.resolve().relative_to(host.resolve())
        copied_asset.unlink()
        records.append(dict(file=relative.as_posix(), asset=asset, sha256=hashlib.sha256(file.read_bytes()).hexdigest()))
    return dict(source=str(mirror), inputs=records)


def refresh_plugins(build: Path, host: Path) -> None:
    host = host.resolve()
    host.relative_to((ROOT / "build").resolve())
    if not (host / "ReproductionInputs.json").is_file():
        raise ValueError(f"not an owned reproduction host: {host}")
    for name in PLUGINS:
        source, target = build.resolve() / "Plugins" / name, host / "Plugins" / name
        identity = json.loads((source / "BuildIdentity.json").read_text(encoding="utf-8"))
        assert fingerprint(source) == identity["source_fingerprint"], name
        for folder in ("Source", "Config", "Resources"):
            sync_directory(source / folder, target / folder, host)
        for filename in (f"{name}.uplugin", "BuildIdentity.json", "Binaries/Win64/UnrealEditor.modules",
                         f"Binaries/Win64/UnrealEditor-{name}.dll"):
            shutil.copy2(source / filename, target / filename)
        assert fingerprint(target) == identity["source_fingerprint"], name
    print(f"Refreshed verified plugin copies: {host}")


def prepare(project: Path, build: Path, host: Path) -> Path:
    project, build, host = project.resolve(), build.resolve(), host.resolve()
    host.relative_to((ROOT / "build").resolve())
    if host.exists():
        raise ValueError(f"choose a new validation host; existing evidence is retained: {host}")
    source = project.parent
    descriptor = json.loads(project.read_text(encoding="utf-8-sig"))
    host.mkdir(parents=True)
    for folder in ("Content", "Config"):
        copy_directory(source / folder, host / folder)
    for plugin in (source / "Plugins").iterdir():
        if plugin.is_dir() and plugin.name not in PLUGINS:
            copy_directory(plugin, host / "Plugins" / plugin.name)
    stage_plugins(build, host)
    descriptor["Plugins"] = [item for item in descriptor.get("Plugins", []) if item["Name"] not in PLUGINS]
    descriptor["Plugins"].extend(dict(Name=name, Enabled=True) for name in PLUGINS)
    result = host / "NexusValidation.uproject"
    result.write_text(json.dumps(descriptor, indent=2) + "\n", encoding="utf-8")
    record = dict(source_project=str(project), plugin_build=str(build), replay=stage_replay(source, host))
    (host / "ReproductionInputs.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--source-project", type=Path)
    action.add_argument("--refresh-plugins", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/validation")
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    if args.refresh_plugins:
        refresh_plugins(args.build_dir, args.host)
    else:
        print(prepare(args.source_project, args.build_dir, args.host))


if __name__ == "__main__":
    main()
