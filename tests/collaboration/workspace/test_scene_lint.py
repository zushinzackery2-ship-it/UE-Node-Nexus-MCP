"""Scene files exercise the workspace entrypoint and the scene model validator."""

from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout
from ue_node_nexus_mcp.transcode.collaboration.workspace.commands import run
from tests.collaboration.helpers import repository


def test_new_scene_in_workspace_uses_scene_validation(tmp_path):
    store, _ = repository(tmp_path)
    workspace = checkout(store, "main", agent_id="scene-author")
    file = workspace.root / "Scenes/Maps/Gallery/Gallery.scene.nexus"
    file.parent.mkdir(parents=True)
    file.write_text("""nexus: 1
asset: /Game/Maps/Gallery
class: World
schema: test-schema

[scene]
Name = "Gallery"

[actors]
camera : /Script/Engine.CameraActor
""", encoding="utf-8")
    result = run(workspace, "lint", ["Scenes/Maps/Gallery/Gallery.scene.nexus"], dict())
    assert result["error_count"] == 0, result
    assert result["ok_files"] == 1
    file.write_text(file.read_text(encoding="utf-8").replace('Name = "Gallery"', 'Name = "Gallery"\nUnknown = "Gallery"'), encoding="utf-8")
    rejected = run(workspace, "lint", ["Scenes/Maps/Gallery/Gallery.scene.nexus"], dict())
    assert rejected["error_count"] > 0
    assert "scene" in str(rejected["diagnostics"])
