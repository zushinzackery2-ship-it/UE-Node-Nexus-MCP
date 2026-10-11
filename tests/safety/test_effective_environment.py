"""Effective config, live cvars and launch options are required evidence."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.safety.publication.evidence import binding, verify
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.safety.test_publication_evidence import builds, environment, fixture


def test_hashes_without_effective_measurements_cannot_admit_publication():
    item, _, _, _ = fixture()
    legacy = environment()
    for key in ("effective_environment_contract", "effective_configuration", "render_cvars", "render_launch"):
        legacy.pop(key, None)
    with pytest.raises(SyncError, match="effective"):
        binding(item, "candidate", dict(revisions={}), builds(), "d3d12", legacy)


def test_unreproducible_render_command_line_is_rejected_before_start():
    item, _, _, _ = fixture()
    source = environment()
    source["render_launch"] = dict(arguments=[], unsupported=["-vulkan"])
    with pytest.raises(SyncError, match="launch"):
        binding(item, "candidate", dict(revisions={}), builds(), "d3d12", source)


@pytest.mark.parametrize("key", ["effective_configuration", "render_cvars", "render_launch"])
def test_worker_effective_drift_blocks_publication(key):
    _, expected, operation, result = fixture()
    result = deepcopy(result)
    result["data"]["worker"]["environment"][key] = dict(changed="worker")
    with pytest.raises(SyncError, match="configuration differs"):
        verify(result, expected, operation)
