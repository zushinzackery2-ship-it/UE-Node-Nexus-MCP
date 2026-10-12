"""Reclamation keeps live copies and existing evidence intact."""

import json

from ue_node_nexus_mcp.safety.isolation import retention


def run(tmp_path, state="executing"):
    root = tmp_path / "validation" / "run"
    (root / "Project").mkdir(parents=True)
    (root / "Project/Probe.uproject").write_text("{}")
    report = dict(validation_id="run", state=state,
                  worker=dict(instance=dict(pid=42, process_created="time", executable="editor.exe",
                                            project_path=str(root / "Project/Probe.uproject"))))
    (root / "receipt.json").write_text(json.dumps(report))
    return root, report


def test_age_does_not_allow_moving_a_live_worker(tmp_path, monkeypatch):
    root, report = run(tmp_path)
    monkeypatch.setattr(retention, "is_alive", lambda identity: True)
    assert retention.sweep(root.parent, older_than=0) == []
    assert not retention.retain(root.parent, root, report)["retained"]
    assert (root / "Project").is_dir()


def test_dead_interrupted_worker_is_marked_and_concentrated(tmp_path, monkeypatch):
    root, _ = run(tmp_path)
    monkeypatch.setattr(retention, "is_alive", lambda identity: False)
    assert len(retention.sweep(root.parent, older_than=0)) == 1
    report = json.loads((root / "receipt.json").read_text())
    assert report["state"] == "interrupted" and report["finished_at"]
    assert (root.parent / ".del/run/Project/Probe.uproject").is_file()
    assert retention.sweep(root.parent, older_than=0) == []


def test_unknown_startup_identity_stays_in_place(tmp_path):
    root, report = run(tmp_path, "starting")
    report.pop("worker")
    (root / "receipt.json").write_text(json.dumps(report))
    assert retention.sweep(root.parent, older_than=0) == []
    assert (root / "Project").is_dir()


def test_retention_never_overwrites_previous_evidence(tmp_path, monkeypatch):
    root, report = run(tmp_path, "validated")
    monkeypatch.setattr(retention, "is_alive", lambda identity: False)
    destination = root.parent / ".del/run/Project"
    destination.mkdir(parents=True)
    (destination / "original.txt").write_text("keep")
    assert not retention.retain(root.parent, root, report)["retained"]
    assert (root / "Project").is_dir() and (destination / "original.txt").read_text() == "keep"


def test_copying_owner_must_exit_before_sweep(tmp_path, monkeypatch):
    root, report = run(tmp_path, "copying")
    report.pop("worker")
    report["owner"] = dict(pid=43, process_created="time", executable="python.exe")
    (root / "receipt.json").write_text(json.dumps(report))
    monkeypatch.setattr(retention, "is_alive", lambda identity: True)
    assert retention.sweep(root.parent, older_than=0) == []
    monkeypatch.setattr(retention, "is_alive", lambda identity: False)
    assert len(retention.sweep(root.parent, older_than=0)) == 1
