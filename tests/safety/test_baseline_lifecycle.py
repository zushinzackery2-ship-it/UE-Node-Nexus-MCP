"""Consumers own partial exports, preflight failures and interrupted snapshots."""

import json
from pathlib import Path

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.safety.isolation import baselines


def project(tmp_path):
    descriptor = tmp_path / "Project/Probe.uproject"
    descriptor.parent.mkdir()
    descriptor.write_text("{}")
    return descriptor


def bridge_for(lease, failure=False):
    def bridge(operation, payload):
        assert operation == "safety_baseline_export" and payload["baseline_id"] == lease.identifier
        file = lease.root / "Content/Probe.uasset"
        file.parent.mkdir(parents=True)
        file.write_bytes(b"unsaved package")
        if failure:
            return dict(ok=False, error=dict(code="export_failed", message="failed after first package"))
        return dict(ok=True, data=dict(root=str(lease.root), project=str(lease.project), files=[], dry_run=False))
    return bridge


@pytest.mark.parametrize("phase", ["success", "export", "preflight"])
def test_export_payload_archived_with_evidence_on_every_exit(tmp_path, phase):
    lease = baselines.BaselineLease(str(project(tmp_path)))
    def run():
        with lease:
            lease.export(bridge_for(lease, phase == "export"), ["/Game/Probe"])
            if phase == "preflight":
                raise InstanceError("isolation_disk_budget", "target volume is full")
            lease.bind(dict(data=dict(validation_id="validation", receipt_path="receipt.json", state="validated")))
    if phase == "success":
        run()
    else:
        with pytest.raises(Exception, match="failed after first package|target volume"):
            run()
    record = json.loads(lease.journal.read_text())
    assert record["state"] == "archived" and not lease.root.exists()
    assert (Path(record["holding"]) / "Content/Probe.uasset").read_bytes() == b"unsaved package"
    if phase == "success":
        assert record["validation_id"] == "validation" and record["baseline_digest"]
    else:
        assert record["error"]


def test_live_owner_prevents_recovery_even_after_a_long_time(tmp_path, monkeypatch):
    lease = baselines.BaselineLease(str(project(tmp_path)))
    with lease:
        lease.export(bridge_for(lease), ["/Game/Probe"])
        monkeypatch.setattr(baselines, "is_alive", lambda identity: True)
        assert baselines.sweep(lease.project) == []
        assert lease.root.exists()


def test_dead_consumer_recovery_archives_exact_identity_once(tmp_path, monkeypatch):
    lease = baselines.BaselineLease(str(project(tmp_path)))
    lease.__enter__()
    lease.export(bridge_for(lease), ["/Game/Probe"])
    lease.lock.__exit__(None, None, None)
    monkeypatch.setattr(baselines, "is_alive", lambda identity: False)
    assert baselines.sweep(lease.project) == [lease.identifier]
    record = json.loads(lease.journal.read_text())
    assert record["interrupted"] and record["state"] == "archived"
    assert baselines.sweep(lease.project) == []


def test_existing_archive_is_never_overwritten(tmp_path):
    lease = baselines.BaselineLease(str(project(tmp_path)))
    with pytest.raises(InstanceError, match="already exists"):
        with lease:
            lease.export(bridge_for(lease), ["/Game/Probe"])
            destination = lease.project.parent / ".del/SafetyBaseline" / lease.identifier
            destination.mkdir(parents=True)
            (destination / "keep.txt").write_text("original evidence")
    assert lease.root.exists()
    assert json.loads(lease.journal.read_text())["state"] == "archive_failed"
    assert (destination / "keep.txt").read_text() == "original evidence"


def test_tampered_lease_cannot_move_another_directory(tmp_path):
    lease = baselines.BaselineLease(str(project(tmp_path)))
    lease.record["root"] = str(tmp_path)
    with pytest.raises(InstanceError, match="different storage"):
        baselines.archive(lease.project, lease.record)
