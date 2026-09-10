"""Replay the captured 30-asset Forge batch with real compile/save barriers."""

from __future__ import annotations

import json
from pathlib import Path
import time

from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.model import Prop
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.paths import object_path
from ue_node_nexus_mcp.transcode.state import SyncState
from ue_node_nexus_mcp.transcode.sync import run_sync


def _property(document, section: str, key: str, value: str) -> None:
    target = document.ensure_section(section)
    target.entries = [entry for entry in target.entries if not isinstance(entry, Prop) or entry.key != key]
    target.entries.append(Prop(key, value))


def _prepare(session, root, schema: str) -> list:
    source = session.project.parent / "ReplayInputs/ForgePostProcess"
    target = root / session.project.stem / "ForgePostProcess"
    files = []
    for original in sorted(source.rglob("*.nexus")):
        document, diagnostics = parse(original.read_text(encoding="utf-8"))
        assert not diagnostics.has_errors, diagnostics
        document.header.schema = schema
        file = target / original.relative_to(source)
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(emit(document), encoding="utf-8")
        files.append(file)
    assert len(files) == 30
    return files


def _change(files: list, round_index: int) -> None:
    for file in files:
        document, diagnostics = parse(file.read_text(encoding="utf-8"))
        assert not diagnostics.has_errors, diagnostics
        if document.header.cls == "Material":
            _property(document, "asset", "BlendablePriority", str(100 + round_index))
        else:
            _property(document, "scalar", "Strength", str(1 - round_index / 1000))
        file.write_text(emit(document), encoding="utf-8")


def exercise(session, rounds: int = 3) -> dict:
    root = session.project.parent / "ReplayMirror"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    initialized = run_sync(session.call, "init", options=dict(pull_all=False, auto_export=True), env=env)
    files = _prepare(session, root, initialized["schema_key"])
    selected = [str(file) for file in files]
    lint = run_sync(session.call, "lint", selected, env=env)
    session.report("replay-lint", lint)
    assert lint["error_count"] == 0 and lint["ok_files"] == 30, lint
    dry = run_sync(session.call, "push", selected, env=env)
    session.report("replay-plan", dry)
    assert dry["error_count"] == 0, dry
    records = []
    for index in range(rounds):
        if index:
            _change(files, index)
        started = time.monotonic()
        result = run_sync(session.call, "push", selected,
                          dict(dry_run=False, compile=True, save=True, stop_on_error=True), env=env)
        session.report(f"replay-round-{index + 1}", result)
        assert result["error_count"] == 0 and result["counts"] == dict(pushed=30), result
        status = run_sync(session.call, "status", selected, env=env)
        assert status["counts"] == dict(clean=30), status
        records.append(dict(round=index + 1, assets=30, elapsed_seconds=time.monotonic() - started))
        print(json.dumps(dict(phase="forge_batch_passed", **records[-1])), flush=True)
    assets, instances = [], []
    for file in files:
        document, _ = parse(file.read_text(encoding="utf-8"))
        asset = object_path(document.header.asset)
        assets.append(asset)
        if document.header.cls == "MaterialInstanceConstant":
            instances.append(asset)
        diagnostics = session.require("diagnostics_get", asset_path=asset, severity="all")
        assert diagnostics["error_count"] == 0, diagnostics
    result = dict(mirror=str(root), assets=assets, instances=instances, rounds=records)
    session.report("replay-result", result)
    return result


def verify_reopened(session, expected: dict) -> None:
    env = dict(UE_NEXUS_TRANSCODE_DIR=expected["mirror"])
    status = run_sync(session.call, "status", expected["assets"], env=env)
    session.report("replay-reopened", status)
    assert status["counts"] == dict(clean=30), status
    state = SyncState.load(Path(expected["mirror"]) / session.project.stem)
    ordered = sorted(expected["assets"], key=lambda asset: (state.assets[asset].kind == "material_instance", asset))
    for asset in ordered:
        report = session.require("asset_validate", asset_path=asset)
        assert not report.get("error_count", 0), report
    session.report("replay-reopened-validation-order", ordered)
