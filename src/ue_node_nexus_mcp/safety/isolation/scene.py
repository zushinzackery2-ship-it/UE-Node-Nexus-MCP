"""Construct, save and export a scene through the worker's native commit."""

import json
from pathlib import Path
from uuid import uuid4

from ...instances.errors import require
from ...transcode.storage.io import atomic_write, canonical
from ...transcode.storage.paths import content_parts
from .publication import bind_repository


def execute(session, item: dict) -> dict:
    source = Path(item["payload"]["plan_file"]).resolve()
    require(source.is_relative_to(session.project.parent.resolve()) and source.is_file(),
        "isolation_path_rejected", "scene plan must belong to the copied project")
    plan = json.loads(source.read_text(encoding="utf-8-sig"))
    map_path, name = plan["map_path"], plan["name"]
    targets = {map_path}
    targets.update(row["level_path"] for row in plan.get("actors", []))
    targets.update(row["level_path"] for row in plan.get("managed_actors", []))
    require(all((parts := content_parts(path)) is not None and parts[0] == "Game" for path in targets),
        "isolation_target_rejected", "scene writes must stay in copied /Game map packages")
    root, repository = bind_repository(session)
    # Startup preview edits belong to this private copy, outside the candidate scene.
    opened = session.call("level_open", dict(map_path=map_path, dry_run=False, discard_changes=True))
    require(opened.get("ok"), "isolation_scene_unavailable", "worker could not open the candidate map", response=opened)
    identifier = uuid4().hex
    before = root / "Before" / identifier / "scene.json"
    before.parent.mkdir(parents=True)
    selector = dict(plan["selector"], map_path=map_path, name=name, out_file=str(before))
    exported = session.call("scene_export", selector)
    require(exported.get("ok") and before.is_file(), "isolation_readback_failed",
        "worker could not observe the scene baseline", response=exported)
    baseline = json.loads(before.read_text(encoding="utf-8-sig"))
    require(baseline.get("map_path") == map_path and baseline.get("name") == name,
        "isolation_identity_mismatch", "scene baseline names a different map or group")
    plan["expected_revision"] = baseline["revision"]
    plan["request_token"] = identifier
    directory = repository / "transactions" / identifier
    plan_file = directory / "plan.json"
    output = directory / "after" / "scene.json"
    output.parent.mkdir(parents=True)
    atomic_write(plan_file, canonical(plan))
    payload = dict(plan_file=str(plan_file), out_file=str(output), dry_run=False, save=True,
        apply_id=identifier, repository=str(repository), collaboration_version=1,
        expected_revision=baseline["live_revision"], kind="scene", asset_path=map_path,
        scene_plan=plan, selector=dict(plan["selector"], map_path=map_path))
    response = session.call("scene_apply", payload)
    if not response.get("ok"):
        return response
    data = response.get("data", {})
    receipt = data.get("receipt", {})
    require(receipt.get("phase") == "ue_committed" and receipt.get("apply_id") == identifier,
        "isolation_commit_unverified", "scene requires a committed native receipt")
    require(receipt.get("request") == dict(payload, operation="scene_apply"),
        "isolation_commit_unverified", "native scene receipt differs from the executed plan")
    raw = receipt.get("after")
    require(isinstance(raw, dict) and raw.get("map_path") == map_path and raw.get("name") == name,
        "isolation_readback_failed", "saved scene names another map or group")
    # Native commit canonicalizes the scene; attempt identity belongs to its response.
    response_data = receipt.get("response_data")
    require(data.get("request_token") == identifier and isinstance(response_data, dict)
        and response_data.get("request_token") == identifier,
        "isolation_commit_unverified", "scene response differs from the committed attempt")
    require(data.get("saved") is True and data.get("applied") == len(plan["ops"])
        and not data.get("failed_packages"), "isolation_commit_unverified", "scene construction or saving is incomplete")
    file = Path(data.get("file", "")).resolve()
    require(file == output.resolve() and file.is_file()
        and json.loads(file.read_text(encoding="utf-8-sig")) == raw,
        "isolation_readback_failed", "exported scene differs from its native saved receipt")
    data["isolated_publication"] = dict(apply_id=identifier, construction_verified=True, saved=True,
        readback_verified=True, raw=raw, payload=payload, receipt=receipt)
    return response
