"""Project keys shared with the native startup guard (SHA-256 of UTF-8 path)."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path

from ..errors import InstanceError


def canonical_path(path: str | Path, *, must_exist: bool = True) -> str:
    resolved = Path(path).expanduser().resolve(strict=must_exist)
    if os.name != "nt":
        return resolved.as_posix()
    from .windows import final_path
    return final_path(resolved, must_exist=must_exist)


def project_identity(path: str | Path) -> dict:
    try:
        canonical = canonical_path(path)
    except OSError as exc:
        raise InstanceError("project_not_found", str(exc), dict(project_path=str(path))) from exc
    if Path(canonical).suffix.lower() != ".uproject" or not Path(canonical).is_file():
        raise InstanceError("invalid_project", "project_path must name an existing .uproject file")
    key = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    return dict(project_key=key, project_path=canonical, project_name=Path(canonical).stem)


def runtime_root() -> Path:
    configured = os.environ.get("UE_NEXUS_RUNTIME_DIR")
    if configured:
        return Path(configured).expanduser().resolve()
    parent = Path(os.environ["LOCALAPPDATA"]) if os.name == "nt" else Path.home() / ".cache"
    return parent / "UE-Node-Nexus-MCP" / "Runtime"


def pipe_address(root: Path | None = None) -> str:
    from .processes import user_identity
    root = root or runtime_root()
    scope = hashlib.sha256(str(root.resolve()).lower().encode()).hexdigest()[:16]
    return "\\\\.\\pipe\\UeNodeNexusManager." + user_identity() + "." + scope
