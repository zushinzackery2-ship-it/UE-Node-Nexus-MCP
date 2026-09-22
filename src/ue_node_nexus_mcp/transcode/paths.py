"""Mirror root resolution and asset-path <-> file-path mapping.

Layout::

    <root>/.nexus/schema/<key>/          shared schema lock
    <root>/<Project>/A/B/M_X.mat.nexus   text
    <root>/<Project>/.nexus/base/A/B/M_X.json
    <root>/<Project>/.nexus/state.json
    <root>/<Project>/.nexus/undo/<stamp>/...
"""

from __future__ import annotations

import os
import re
from pathlib import Path, PurePosixPath, PureWindowsPath

ROOT_ENV = "UE_NEXUS_TRANSCODE_DIR"
DEFAULT_ROOT_NAME = "Content_Transcoded"
NEXUS_DIR = ".nexus"

KIND_SUFFIX = {
    "material": "mat",
    "material_function": "mf",
    "material_instance": "mi",
    "blueprint": "bp",
    "niagara_system": "ns",
    "niagara_emitter": "ne",
    "asset": "asset",
    "stub": "stub",
}
SUFFIX_KIND = {suffix: kind for kind, suffix in KIND_SUFFIX.items()}
_SUFFIX_RE = re.compile(r"^(?P<stem>.+)\.(?P<suffix>" + "|".join(sorted(KIND_SUFFIX.values())) + r")\.nexus$")
_GAME_ROOT = "/Game"


def resolve_root(env: dict[str, str] | None = None, cwd: Path | None = None) -> Path:
    environment = os.environ if env is None else env
    configured = environment.get(ROOT_ENV, "").strip()
    if configured:
        return Path(configured).expanduser().resolve()
    return (cwd or Path.cwd()).resolve() / DEFAULT_ROOT_NAME


def schema_dir(root: Path, schema_key: str) -> Path:
    return root / NEXUS_DIR / "schema" / schema_key


def project_label(text: str | None) -> str:
    """A project is named by its ``.uproject`` stem, however the caller spells it.

    Callers reasonably pass the same value they gave the editor - a full
    ``.uproject`` path - and comparing that against a bare project name reads as
    "you bound the wrong project" when the only difference is the spelling.
    """
    value = (text or "").strip().strip('"')
    if not value:
        return ""
    if value.lower().endswith(".uproject") or "/" in value or "\\" in value:
        # Windows semantics parse both separators, so one spelling works anywhere.
        return PureWindowsPath(value).stem
    return value


def project_dir(root: Path, project_name: str) -> Path:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", project_label(project_name)) or "Project"
    return root / safe


def nexus_dir(project: Path) -> Path:
    return project / NEXUS_DIR


def state_path(project: Path) -> Path:
    return nexus_dir(project) / "state.json"


def project_info_path(project: Path) -> Path:
    return project / "project.json"


def undo_dir(project: Path) -> Path:
    return nexus_dir(project) / "undo"


def pending_dir(project: Path) -> Path:
    return nexus_dir(project) / "pending"


def package_name(asset_path: str) -> str:
    """``/Game/A/B/M_X.M_X`` or ``/Game/A/B/M_X`` -> ``/Game/A/B/M_X``."""
    text = asset_path.strip()
    if "'" in text:
        text = text.split("'", 2)[1] if text.count("'") >= 2 else text.replace("'", "")
    dot = text.find(".", text.rfind("/") + 1)
    slash = text.rfind("/")
    if dot > slash:
        text = text[:dot]
    return text


def object_path(asset_path: str) -> str:
    package = package_name(asset_path)
    return f"{package}.{package.rsplit('/', 1)[-1]}"


def asset_relative(asset_path: str) -> PurePosixPath | None:
    package = package_name(asset_path)
    if not package.startswith(_GAME_ROOT + "/"):
        return None
    return PurePosixPath(package[len(_GAME_ROOT) + 1:])


def text_path(project: Path, asset_path: str, kind: str) -> Path:
    relative = asset_relative(asset_path)
    if relative is None:
        raise ValueError(f"only /Game assets are mirrored: {asset_path}")
    suffix = KIND_SUFFIX[kind]
    return project.joinpath(*relative.parts[:-1]) / f"{relative.name}.{suffix}.nexus"


def base_path(project: Path, asset_path: str) -> Path:
    relative = asset_relative(asset_path)
    if relative is None:
        raise ValueError(f"only /Game assets are mirrored: {asset_path}")
    return nexus_dir(project).joinpath("base", *relative.parts[:-1]) / f"{relative.name}.json"


def parse_text_path(project: Path, path: Path) -> tuple[str, str] | None:
    """Mirror file -> (object path, kind); None when the file is not a mirror file."""
    try:
        relative = path.resolve().relative_to(project.resolve())
    except ValueError:
        return None
    if not relative.parts or relative.parts[0] == NEXUS_DIR:
        return None
    match = _SUFFIX_RE.match(relative.name)
    if match is None:
        return None
    stem = match.group("stem")
    package = "/".join([_GAME_ROOT, *relative.parts[:-1], stem])
    return f"{package}.{stem}", SUFFIX_KIND[match.group("suffix")]


def iter_text_files(project: Path) -> list[Path]:
    if not project.is_dir():
        return []
    files = [path for path in project.rglob("*.nexus") if NEXUS_DIR not in path.relative_to(project).parts]
    return sorted(files)


def is_within(root: Path, path: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def display_path(project: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(project.resolve()).as_posix()
    except ValueError:
        return path.as_posix()
