"""Repository-owned transactions can be abandoned by another workspace."""

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.recover import discard
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout
from tests.collaboration.helpers import ASSET, repository


@pytest.mark.parametrize("owner_state", ["active", "closed", "missing"])
def test_abort_from_other_workspace(tmp_path, owner_state):
    store, root = repository(tmp_path)
    caller, owner = checkout(store, root), checkout(store, root)
    if owner_state == "closed":
        owner.persist(dict(owner.state, closed=True))
    if owner_state == "missing":
        store.drop_record("workspace", owner.state["id"], owner.state["generation"])
    store.put_record("apply", "pending-owner", dict(id="pending-owner", workspace_id=owner.state["id"],
        asset=ASSET, source=root, candidate=root, target=root, phase="recovery_required"), [root])
    result = discard(caller, dict(apply_id="pending-owner", dry_run=False))
    assert result["phase"] == "rejected"
    if owner_state != "active":
        assert result["consume"] is False

