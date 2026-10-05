"""Execute the same native commit, compile, save and readback inside the worker."""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
from uuid import uuid4

from ...instances.errors import require
from ...transcode.storage.paths import object_path


def execute(session, item: dict) -> dict:
    identifier = uuid4().hex
    root = session.project.parent / "Saved/Nexus/ValidationPublication"
    root.mkdir(parents=True, exist_ok=True)
    repository = root / "Repository"
    repository.mkdir(exist_ok=True)
    bound = session.call("transcode_root_set", dict(root=str(root), collaboration_root=str(repository), project_id=identifier))
    require(bound.get("ok") and bound.get("data", dict()).get("collaboration_version") == 1,
            "isolation_protocol_mismatch", "worker could not bind its publication repository", response=bound)
    payload = deepcopy(item["payload"])
    asset = payload["asset_path"]
    kind = payload.get("kind", "niagara_system" if item["operation"] == "vfx_transcode_apply" else "material")
    output = repository / "transactions" / identifier / "after"
    before = root / "Before" / identifier
    before.mkdir(parents=True)
    export_operation = "vfx_transcode_export" if kind == "niagara_system" else "transcode_export"
    exported = session.call(export_operation, dict(asset_paths=[asset], out_dir=str(before), include_stubs=True))
    require(exported.get("ok"), "isolation_readback_failed", "worker could not observe the candidate baseline", response=exported)
    rows = exported.get("data", dict()).get("assets", [])
    payload.pop("expected_revision", None)
    payload.pop("expected_absent", None)
    if rows:
        file = Path(rows[0]["file"])
        file.resolve().relative_to(before.resolve())
        raw = json.loads(file.read_text(encoding="utf-8-sig"))
        require(object_path(raw["asset_path"]) == object_path(asset), "isolation_identity_mismatch", "baseline export names a different asset")
        payload["expected_revision"] = raw["live_revision"]
    else:
        absent = exported.get("data", dict()).get("skipped", [])
        require(any(row.get("reason") == "asset_not_found" and object_path(row["asset_path"]) == object_path(asset) for row in absent),
                "isolation_readback_failed", "baseline must be explicitly present or absent")
        payload["expected_absent"] = True
    payload.update(apply_id=identifier, collaboration_version=1, repository=str(repository),
                   dry_run=False, compile=True, save=True, out_dir=str(output), kind=kind)
    response = session.call(item["operation"], payload)
    if not response.get("ok"):
        return response
    data = response.get("data", dict())
    receipt = data.get("receipt", dict())
    require(receipt.get("phase") == "ue_committed" and receipt.get("apply_id") == identifier,
            "isolation_commit_unverified", "worker operation requires a committed native receipt")
    require(receipt.get("request") == dict(payload, operation=item["operation"]),
            "isolation_commit_unverified", "native receipt differs from the executed candidate")
    raw = receipt.get("after")
    require(isinstance(raw, dict) and object_path(raw.get("asset_path", "")) == object_path(asset),
            "isolation_readback_failed", "native saved readback is missing or names another asset")
    file = Path(data.get("file", ""))
    file.resolve().relative_to(output.resolve())
    require(file.is_file() and json.loads(file.read_text(encoding="utf-8-sig")) == raw,
            "isolation_readback_failed", "exported saved result differs from the native receipt")
    data["isolated_publication"] = dict(apply_id=identifier, compiled=True, saved=True, readback_verified=True,
                                        raw=raw, payload=payload, receipt=receipt)
    return response
