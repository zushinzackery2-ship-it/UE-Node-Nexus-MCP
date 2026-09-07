from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.sync import run_sync

from .fake_ue import FakeUe
from .fixtures import material_function_raw, material_raw


@pytest.fixture
def sync_workspace(tmp_path: Path):
    ue = FakeUe(dict(mf=material_function_raw(), mat=material_raw()))
    root = tmp_path / "Content_Transcoded"
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root))
    run_sync(ue, "init", env=env)
    return ue, env, root / "Shadetest"
