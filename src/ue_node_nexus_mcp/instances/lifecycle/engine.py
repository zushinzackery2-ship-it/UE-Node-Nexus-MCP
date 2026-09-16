"""Deterministic engine and launch-profile resolution."""

from __future__ import annotations

import json
from pathlib import Path
import winreg

from ..errors import InstanceError, require
from ..identity.paths import canonical_path
from ...build_info.requirements import CONTRACT_VERSION
from .installation import verify


def resolve_engine(project: str, configured: str | None) -> Path:
    if configured:
        root = Path(configured).resolve(strict=True)
        return root.parent if root.name.lower() == "engine" else root
    descriptor = json.loads(Path(project).read_text(encoding="utf-8-sig"))
    association = descriptor.get("EngineAssociation", "")
    locations = [(winreg.HKEY_CURRENT_USER, r"Software\Epic Games\Unreal Engine\Builds", association),
                 (winreg.HKEY_LOCAL_MACHINE, "SOFTWARE\\EpicGames\\Unreal Engine\\" + association, "InstalledDirectory")]
    for hive, path, name in locations:
        try:
            with winreg.OpenKey(hive, path) as key:
                return Path(winreg.QueryValueEx(key, name)[0]).resolve(strict=True)
        except FileNotFoundError:
            continue
    raise InstanceError("engine_required", "set engine_path for this project's EngineAssociation", dict(association=association))


def launch_options(project: dict, payload: dict) -> dict:
    profile = payload.get("launch_profile", "offscreen")
    require(profile in ("interactive", "offscreen"), "invalid_request", "unknown launch profile")
    root = resolve_engine(project["project_path"], payload.get("engine_path"))
    executable = root / "Engine/Binaries/Win64" / ("UnrealEditor.exe" if profile == "interactive" else "UnrealEditor-Cmd.exe")
    require(executable.is_file(), "engine_not_found", "editor executable does not exist", executable=str(executable))
    verify(project, root)
    rhi = payload.get("rhi") or "d3d12"
    require(rhi in ("d3d12", "d3d11", "nullrhi"), "invalid_request", "unknown RHI requirement")
    return dict(executable=canonical_path(executable), engine_dir=canonical_path(root / "Engine"),
                launch_profile=profile, rhi=rhi)


def compatible(instance: dict, payload: dict) -> None:
    require(instance.get("guard_protocol") == 1 and instance.get("contract_version") == CONTRACT_VERSION,
            "instance_incompatible", "loaded editor must implement lifecycle protocol 1 and bridge contract 4",
            guard_protocol=instance.get("guard_protocol"), contract_version=instance.get("contract_version"))
    if payload.get("engine_path"):
        expected = resolve_engine(instance["project_path"], payload["engine_path"]) / "Engine"
        require(canonical_path(expected) == instance["engine_dir"], "instance_incompatible", "different engine is already running")
    if payload.get("rhi"):
        require(payload["rhi"] == instance.get("rhi"), "instance_incompatible", "running RHI does not meet the requirement")
    if payload.get("launch_profile") == "interactive":
        require(instance.get("launch_profile") == "interactive", "instance_incompatible", "drain and restart to change the display profile")
