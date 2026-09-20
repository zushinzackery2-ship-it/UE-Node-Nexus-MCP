"""Durable apply requests, native receipts and atomic publication metadata."""

from __future__ import annotations

from copy import deepcopy
from uuid import uuid4

from ...paths import object_path
from ...sync_project import SyncError, apply_operation
from ..semantic.snapshot import from_raw
from ..store.io import atomic_write, canonical, digest
from ..store.refs import move_ref
from .receipt import verify_request


def save(workspace, record: dict) -> None:
    roots = [record[key] for key in ("source", "candidate", "target", "published") if record.get(key)]
    record["generation"] = workspace.store.put_record("apply", record["id"], record, roots, record.get("generation", 0))
    directory = workspace.store.root / "transactions" / record["id"]
    atomic_write(directory / "apply.json", canonical(record))
    workspace.store.event("apply", workspace_id=workspace.state["id"], apply_id=record["id"], phase=record["phase"], asset=record["asset"])


def request(workspace, item: dict, source: str, candidate: str, target: str, observation: dict, options: dict, *, consume=True) -> dict:
    identifier = uuid4().hex
    directory = workspace.store.root / "transactions" / identifier
    payload = deepcopy(item["payload"])
    payload.update(apply_id=identifier, collaboration_version=1, repository=str(workspace.store.root), dry_run=False,
                   compile=options.get("compile", True), save=True, out_dir=str(directory / "after"))
    live = observation["revisions"].get(item["asset"])
    if live:
        payload["expected_revision"] = live["revision"]
    else:
        payload["expected_absent"] = True
    read_set = []
    entries = workspace.history.entries(target)
    for dependency in item["dependencies"]:
        revision = observation["revisions"].get(dependency)
        if not revision:
            raise SyncError("read_set_missing", "dependency needs a fresh observation", dict(asset=item["asset"], dependency=dependency))
        snapshot = workspace.store.objects.data(entries[dependency], "snapshot")
        read_set.append(dict(asset_path=dependency, kind=snapshot["semantic"]["kind"], expected_revision=revision["revision"]))
    payload["read_set"] = read_set
    operation = apply_operation(item["kind"])
    if item["kind"] == "scene":
        plan = dict(item["scene_plan"], request_token=identifier)
        plan_file = directory / "plan.json"
        atomic_write(plan_file, canonical(plan))
        payload.update(plan_file=str(plan_file), out_file=str(directory / "after" / "scene.json"))
        operation = "scene_apply"
    record = dict(id=identifier, workspace_id=workspace.state["id"], asset=item["asset"], kind=item["kind"], phase="prepared",
                  source=source, candidate=candidate, target=target, request=payload, operation=operation,
                  request_digest=digest(payload), generation=0, response=None, consume=consume)
    save(workspace, record)
    return record


def envelope(value) -> dict:
    if not isinstance(value, dict):
        return dict(ok=False, error=dict(code="protocol_mismatch", message="bridge returned a non-object response"))
    return value


def verify(workspace, record: dict, receipt) -> dict | None:
    """A receipt counts only for the request, asset and schema it names."""
    if not isinstance(receipt, dict):
        return None
    if receipt.get("apply_id") != record["id"]:
        raise SyncError("receipt_invalid", "receipt belongs to another execution",
                        dict(apply_id=record["id"], received=receipt.get("apply_id")))
    verify_request(record, receipt)
    after = receipt.get("after")
    if isinstance(after, dict):
        reported = after.get("asset_path")
        if reported and object_path(reported) != record["asset"]:
            raise SyncError("receipt_invalid", "receipt reports another asset",
                            dict(apply_id=record["id"], expected=record["asset"], received=reported))
        key = after.get("schema_key")
        if key and workspace.schema and key != workspace.schema.key:
            raise SyncError("schema_stale", "result was produced under another schema environment",
                            dict(apply_id=record["id"], expected=workspace.schema.key, received=key))
    return receipt


def execute(bridge, workspace, record: dict) -> dict:
    record["phase"] = "applying"
    save(workspace, record)
    try:
        response = envelope(bridge(record["operation"], record["request"]))
    except (OSError, SyncError) as exc:
        response = dict(ok=False, error=dict(code="transport_lost", message=str(exc)))
    record["response"] = response
    receipt = (response.get("data") or dict()).get("receipt")
    error_code = (response.get("error") or dict()).get("code")
    preflight_error = error_code in ("stale_target", "protocol_mismatch", "save_required", "idempotency_mismatch")
    if preflight_error and isinstance(receipt, dict) and not receipt.get("apply_id"):
        receipt = None
    if not receipt and not preflight_error and (not response.get("ok") or not response.get("data")):
        try:
            recovered = envelope(bridge("transcode_recover", dict(apply_id=record["id"], repository=str(workspace.store.root))))
            receipt = (recovered.get("data") or dict()).get("receipt")
        except (OSError, SyncError):
            receipt = None
    record["receipt"] = verify(workspace, record, receipt)
    if receipt and receipt.get("phase") == "ue_committed":
        record["phase"] = "ue_committed"
    elif receipt and receipt.get("phase") in ("rolled_back", "rejected"):
        record["phase"] = receipt["phase"]
    elif preflight_error:
        record["phase"] = "rejected"
    else:
        record["phase"] = "recovery_required"
    save(workspace, record)
    if record["phase"] != "ue_committed":
        error = response.get("error") or dict()
        raise SyncError(error.get("code", "recovery_required"), error.get("message", "execution did not produce a committed receipt"),
                        dict(apply_id=record["id"], phase=record["phase"], receipt=receipt))
    return record


def actual_snapshot(workspace, record: dict) -> dict | None:
    receipt = record["receipt"]
    raw = receipt.get("after")
    if not raw:
        raise SyncError("receipt_invalid", "saved receipt has no verified result snapshot", dict(apply_id=record["id"]))
    if raw.get("exists") is False or record["request"].get("delete_scene"):
        return None
    candidate = workspace.history.entries(record["candidate"]).get(record["asset"])
    prior = deepcopy(workspace.store.objects.data(candidate, "snapshot")) if candidate else None
    data = (receipt.get("response") or dict()).get("data") or receipt.get("response_data") or dict()
    if prior:
        for binding in prior.get("bindings", dict()).values():
            physical = data.get("id_map", dict()).get(binding["alias"])
            if physical:
                binding["physical"] = physical
                binding["meta"]["guid"] = physical
    return from_raw(raw, prior, schema=workspace.schema)


def publish(workspace, record: dict, *, recovery_target: str | None = None) -> str:
    if record["phase"] == "completed":
        return record["published"]
    store, history = workspace.store, workspace.history
    target = store.ref("refs/ue/observed") or record["target"]
    expected = recovery_target if recovery_target is not None else record["target"]
    if target != expected:
        raise SyncError("publication_moved", "observation changed before this receipt was published", dict(apply_id=record["id"], expected=record["target"], actual=target))
    actual = actual_snapshot(workspace, record)
    entries = history.entries(target)
    if actual:
        entries[record["asset"]] = workspace.snapshot(actual)
    else:
        entries.pop(record["asset"], None)
    identifier = history.create(history.tree(entries), [target], "Publish " + record["asset"], workspace.state["agent_id"],
                                "publish", source_commit=record["source"], applied_assets=[record["asset"]], apply_id=record["id"], partial=True)
    old_published = store.ref("refs/ue/published")
    record.update(phase="completed", published=identifier)
    with store.db.connection(write=True) as connection:
        move_ref(connection, "refs/ue/published", identifier, old_published, workspace.state["id"], "publish")
        move_ref(connection, "refs/ue/observed", identifier, target, workspace.state["id"], "publish")
        if record.get("consume", True):
            integration = dict(workspace_id=workspace.state["id"], asset=record["asset"], source_commit=record["source"],
                               source_snapshot=history.entries(record["source"]).get(record["asset"]), candidate_snapshot=history.entries(record["candidate"]).get(record["asset"]),
                               published_commit=identifier, apply_id=record["id"])
            key = workspace.state["id"] + ":" + record["asset"]
            previous = store.record("integration", key)
            roots = [record["source"], record["candidate"], identifier]
            store.put_record("integration", key, integration, roots, previous["generation"] if previous else 0, connection)
        record["generation"] = store.put_record("apply", record["id"], record, [record["source"], record["candidate"], identifier], record["generation"], connection)
    atomic_write(store.root / "transactions" / record["id"] / "apply.json", canonical(record))
    store.event("published", workspace_id=workspace.state["id"], apply_id=record["id"], commit_id=identifier)
    return identifier
