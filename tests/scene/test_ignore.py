"""Scene groups share the same ignore policy as other mirror assets."""

import pytest

from ue_node_nexus_mcp.transcode.scene.selection import records


@pytest.mark.parametrize("directory", [".cache", "Maps/.auto-index-mcp", "Backups"])
def test_scene_discovery_excludes_metadata(scene_workspace, directory):
    _, context, state, item = scene_workspace
    cache = context.project / "Scenes" / directory
    cache.mkdir(parents=True)
    (cache / "Backup.scene.nexus").write_text("invalid scene backup", encoding="utf-8")
    if directory == "Backups":
        (context.project / ".nexusignore").write_text("Scenes/Backups/\n", encoding="utf-8")
    assert records(context, state) == [item]


def test_scene_ignore_does_not_hide_registered_groups(scene_workspace):
    _, context, state, item = scene_workspace
    (context.project / ".nexusignore").write_text("Scenes/\n", encoding="utf-8")
    assert records(context, state) == [item]
