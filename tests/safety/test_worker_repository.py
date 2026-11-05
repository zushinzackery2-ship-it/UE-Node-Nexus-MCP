"""Publication workers bind a real repository before broker calls can reuse it."""

import json
from pathlib import Path

from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.repository.binding import validate_repository
from ue_node_nexus_mcp.safety.isolation.publication import execute


class PublicationSession:
    def __init__(self, project):
        self.project = project
        self.project_ids = []

    def call(self, operation, payload):
        if operation == "transcode_root_set":
            validate_repository(Path(payload["collaboration_root"]),
                                project_identity(self.project), payload["project_id"])
            self.project_ids.append(payload["project_id"])
            return dict(ok=True, data=dict(collaboration_version=1))
        if operation == "transcode_export":
            return dict(ok=True, data=dict(assets=[], skipped=[
                dict(asset_path=payload["asset_paths"][0], reason="asset_not_found")]))
        assert operation == "transcode_apply", operation
        after = dict(asset_path=payload["asset_path"], kind=payload["kind"])
        output = Path(payload["out_dir"])
        output.mkdir(parents=True)
        file = output / "after.json"
        file.write_text(json.dumps(after), encoding="utf-8")
        receipt = dict(phase="ue_committed", apply_id=payload["apply_id"],
                       request=dict(payload, operation=operation), after=after)
        return dict(ok=True, data=dict(receipt=receipt, file=str(file)))


def test_multiple_candidates_and_sessions_share_a_verified_repository(tmp_path):
    project = tmp_path / "Probe.uproject"
    project.write_text("{}", encoding="utf-8")
    identifiers = []
    for names in (("First", "Second"), ("Third",)):
        session = PublicationSession(project)
        for name in names:
            item = dict(operation="transcode_apply", payload=dict(
                asset_path=f"/Game/{name}.{name}", kind="material"))
            result = execute(session, item)
            assert result["ok"]
            assert result["data"]["isolated_publication"]["readback_verified"]
        identifiers.extend(session.project_ids)
    assert len(set(identifiers)) == 1
    repository = project.parent / "Saved/Nexus/ValidationPublication/Probe/.nexus/collaboration"
    validate_repository(repository, project_identity(project), identifiers[0])
