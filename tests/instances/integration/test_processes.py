import asyncio
import os

import pytest

from .contention import run


@pytest.mark.skipif(os.name != "nt", reason="real Windows pipe/process integration")
def test_separate_mcp_workspaces_converge_on_one_instance(tmp_path):
    result = asyncio.run(run(tmp_path / "contention", clients=4, rounds=3))
    assert result["ok"] and result["spawned"] == 3
