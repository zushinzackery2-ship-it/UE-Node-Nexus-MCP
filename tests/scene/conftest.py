from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.scene.pull import pull_one
from ue_node_nexus_mcp.transcode.scene.selection import new_record
from ue_node_nexus_mcp.transcode.scene.state import SceneState
from ue_node_nexus_mcp.transcode.sync_project import ProjectContext

from .fake_scene import FakeScene
from .fixtures import ACTOR, MAP, SCHEMA


@pytest.fixture
def scene_workspace(tmp_path: Path):
    bridge = FakeScene()
    root = tmp_path / "Mirror"
    context = ProjectContext(root, "Shadetest", root / "Shadetest", SCHEMA)
    item = new_record(context, MAP, "Group")
    state = SceneState()
    options = dict(scene=dict(map_path=MAP, name="Group", actor_paths=[bridge.scene_actors[ACTOR]["actor_path"]]))
    pull_one(bridge, context, state, item, options)
    return bridge, context, state, state.records[item.key]
