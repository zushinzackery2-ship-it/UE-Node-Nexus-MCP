"""Create, edit, recover and reopen real scene groups through ue_sync."""

from __future__ import annotations

import stat

from .fixtures import author
from .instances import exercise as exercise_instances
from .workspace import SceneWorkspace


def _save_failure(workspace) -> None:
    before = workspace.summary()
    document = workspace.document()
    owner = next(actor for actor in document.section("actors").decls() if dict(actor.props)["Label"].strip('"') == "root")
    owner.props = [(key, "UpdatedRoot" if key == "Label" else value) for key, value in owner.props]
    workspace.write(document)
    intended, accepted = workspace.file.read_bytes(), workspace.base.read_bytes()
    package = workspace.session.project.parent / "Content" / (workspace.map_path.removeprefix("/Game/") + ".umap")
    mode = package.stat().st_mode
    package.chmod(stat.S_IREAD)
    try:
        report = workspace.sync("push", dry_run=False)
        assert report.get("scene_counts", dict()).get("failed") == 1, report
        assert workspace.file.read_bytes() == intended and workspace.base.read_bytes() == accepted, report
    finally:
        package.chmod(mode)
    workspace.push()
    assert workspace.summary()["actors"] == before["actors"]


def exercise(session, map_path: str) -> dict:
    workspace = SceneWorkspace(session, map_path)
    author(workspace)
    authored = workspace.file.read_bytes()
    dry = workspace.sync("push", dry_run=True)
    assert not dry.get("scene_error_count", 0) and workspace.file.read_bytes() == authored, dry
    created = workspace.push()
    assert created["scene_counts"]["pushed"] == 1, created
    initial = workspace.snapshot()
    assert len(initial["actors"]) == 4, initial
    blueprint = next(actor for actor in initial["actors"] if actor["label"] == "blueprint")
    assert float(blueprint["properties"]["Value"]) == 2, blueprint
    noop = workspace.push()
    assert noop["scene_counts"] == dict(unchanged=1), noop
    instance_result = exercise_instances(workspace)
    document = workspace.document()
    for section in document.sections:
        if section.name == "instances":
            section.entries.reverse()
    before = workspace.summary()
    workspace.write(document)
    workspace.push()
    assert workspace.summary()["instances"] == before["instances"]
    _save_failure(workspace)
    status = workspace.sync("status")
    assert status["scene_counts"] == dict(clean=1), status
    result = workspace.summary()
    result["instance_checks"] = instance_result
    session.report("scene-result", result)
    return result


def verify_reopened(session, expected: dict) -> None:
    workspace = SceneWorkspace(session, expected["map_path"], expected["name"])
    actual = workspace.summary()
    assert actual["actors"] == expected["actors"], actual
    assert actual["instances"] == expected["instances"], actual
    status = workspace.sync("status")
    assert status["scene_counts"] == dict(clean=1), status
    session.report("scene-reopened", actual)
