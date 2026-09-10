"""Assemble verified editor binaries and tracked source into release assets."""

from __future__ import annotations

import argparse
from email.parser import BytesParser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from release.identity import fingerprint

PLUGINS = ("UeNodeNexusBridge", "UeNodeNexusVfxBridge")


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def plugin_files(build: Path, plugin: str, version: str, build_id: str, local_build: bool = False) -> dict[str, Path]:
    prefix = f"Plugins/{plugin}"
    source = ROOT / prefix
    built = build / prefix
    descriptor = read_json(source / f"{plugin}.uplugin")
    if descriptor["VersionName"] != version:
        raise RuntimeError(f"plugin version mismatch: {plugin}")
    if descriptor != read_json(built / f"{plugin}.uplugin"):
        raise RuntimeError(f"rebuild required: descriptor changed for {plugin}")
    modules_file = built / "Binaries/Win64/UnrealEditor.modules"
    modules = read_json(modules_file)
    binary_name = f"UnrealEditor-{plugin}.dll"
    if modules.get("BuildId") != build_id or modules.get("Modules", dict()).get(plugin) != binary_name:
        raise RuntimeError(f"incompatible module manifest: {plugin}")
    files = dict()
    tracked = git("ls-files", "-z", "--", prefix).split("\0")
    if local_build:
        tracked = [f"{prefix}/{plugin}.uplugin"]
        for directory in ("Source", "Config", "Resources"):
            tracked.extend(path.relative_to(ROOT).as_posix() for path in (source / directory).rglob("*") if path.is_file())
    for relative in tracked:
        if not relative:
            continue
        local = ROOT / relative
        compiled_source = build / relative
        if not compiled_source.is_file() or sha256(local) != sha256(compiled_source):
            raise RuntimeError(f"rebuild required: {relative}")
        files[relative] = local
    binary = built / "Binaries/Win64" / binary_name
    with binary.open("rb") as stream:
        if stream.read(2) != b"MZ":
            raise RuntimeError(f"invalid Windows DLL: {binary}")
    identity_file = built / "BuildIdentity.json"
    identity = read_json(identity_file)
    source_hash = fingerprint(source)
    if identity.get("source_fingerprint") != source_hash or fingerprint(built) != source_hash:
        raise RuntimeError(f"source fingerprint differs from build: {plugin}")
    if source_hash.encode("utf-16le") not in binary.read_bytes():
        raise RuntimeError(f"DLL does not embed the current source fingerprint: {plugin}")
    if identity.get("version") != version or identity.get("contract_version") != 2:
        raise RuntimeError(f"build identity contract mismatch: {plugin}")
    files[f"{prefix}/BuildIdentity.json"] = identity_file
    files[f"{prefix}/Binaries/Win64/{binary_name}"] = binary
    files[f"{prefix}/Binaries/Win64/UnrealEditor.modules"] = modules_file
    icon = files[f"{prefix}/Resources/Icon128.png"]
    if sha256(icon) != sha256(ROOT / "assets/branding/nexus-128.png"):
        raise RuntimeError(f"outdated plugin icon: {plugin}")
    return files


def verify_wheel(wheel: Path, version: str) -> None:
    with zipfile.ZipFile(wheel) as archive:
        metadata = BytesParser().parsebytes(archive.read(f"ue_node_nexus_mcp-{version}.dist-info/METADATA"))
        if metadata["Version"] != version:
            raise RuntimeError("wheel version mismatch")
        members = set(archive.namelist())
        for module in ("prepare", "apply", "commit", "diagnostics", "recovery", "refresh"):
            if f"ue_node_nexus_mcp/transcode/push/{module}.py" not in members:
                raise RuntimeError(f"wheel missing sync module: {module}")
        source_root = ROOT / "src/ue_node_nexus_mcp"
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix not in (".py", ".json", ".md") or "__pycache__" in path.parts:
                continue
            member = "ue_node_nexus_mcp/" + path.relative_to(source_root).as_posix()
            if member not in members or archive.read(member) != path.read_bytes():
                raise RuntimeError(f"wheel source differs or is missing: {member}")
        if any("Task-Status" in member or ".high-value-information" in member for member in members):
            raise RuntimeError("wheel contains private workspace records")


def package(build: Path, wheel_dir: Path, engine: Path, output: Path, local_build: bool = False) -> None:
    dirty = bool(git("status", "--porcelain"))
    if dirty and not local_build:
        raise RuntimeError("commit the release source before assembling artifacts")
    config = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    version = config["project"]["version"]
    wheel = wheel_dir / f"ue_node_nexus_mcp-{version}-py3-none-any.whl"
    verify_wheel(wheel, version)
    engine_version = read_json(engine / "Engine/Build/Build.version")
    if (engine_version["MajorVersion"], engine_version["MinorVersion"]) != (5, 5):
        raise RuntimeError("this release package targets UE 5.5")
    build_id = read_json(engine / "Engine/Binaries/Win64/UnrealEditor.modules")["BuildId"]
    files = dict()
    for plugin in PLUGINS:
        files.update(plugin_files(build, plugin, version, build_id, local_build))
    for relative in git("ls-files", "-z", "--", "skill").split("\0"):
        if relative:
            files[relative] = ROOT / relative
    files["LICENSE"] = ROOT / "LICENSE"
    files["INSTALL.md"] = ROOT / "release/INSTALL.md"
    files["SCENES.md"] = ROOT / "src/ue_node_nexus_mcp/guides/scene_mirror.md"
    forbidden = (".pdb", ".lib", ".exp", ".obj", ".log", ".tmp", ".uasset", ".umap")
    for name in files:
        parts = Path(name).parts
        if name.endswith(forbidden) or any(part in ("Intermediate", "Saved", ".nexus", ".git") for part in parts):
            raise RuntimeError(f"unwanted release file: {name}")
    output.mkdir(parents=True, exist_ok=True)
    bundle = output / f"UE-Node-Nexus-MCP-v{version}-UE5.5-Win64.zip"
    with zipfile.ZipFile(bundle, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(files.items()):
            archive.write(path, name)
    with zipfile.ZipFile(bundle) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(files):
            raise RuntimeError("release ZIP verification failed")
    published_wheel = output / wheel.name
    shutil.copy2(wheel, published_wheel)
    manifest = output / "release-manifest.json"
    write_json(manifest, dict(
        version=version, tag=None if local_build else f"v{version}-ue5.5", commit=git("rev-parse", "HEAD"),
        source_dirty=dirty, mode="local-build" if local_build else "release", contract_version=2,
        builds=dict((plugin, read_json(build / "Plugins" / plugin / "BuildIdentity.json")) for plugin in PLUGINS),
        engine=engine_version, module_build_id=build_id,
        files=dict((name, dict(size=path.stat().st_size, sha256=sha256(path))) for name, path in sorted(files.items())),
        artifacts=dict((path.name, dict(size=path.stat().st_size, sha256=sha256(path))) for path in (bundle, published_wheel)),
    ))
    checksums = output / "SHA256SUMS.txt"
    checksums.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in (bundle, published_wheel, manifest)), encoding="ascii")
    print(json.dumps(dict(version=version, commit=git("rev-parse", "HEAD"), plugin_files=len(files), output=str(output)), indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/validation")
    parser.add_argument("--wheel-dir", type=Path, required=True)
    parser.add_argument("--engine-dir", type=Path, default=os.environ.get("UE_NEXUS_ENGINE_DIR"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--local-build", action="store_true", help="Package a fingerprint-verified working tree without creating a commit or release tag")
    args = parser.parse_args()
    if args.engine_dir is None:
        parser.error("--engine-dir or UE_NEXUS_ENGINE_DIR is required")
    package(args.build_dir, args.wheel_dir, args.engine_dir, args.output_dir, args.local_build)


if __name__ == "__main__":
    main()
