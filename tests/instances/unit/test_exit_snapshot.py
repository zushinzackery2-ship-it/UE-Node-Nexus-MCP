from ue_node_nexus_mcp.instances.lifecycle.prompts import exited


def test_exited_state_preserves_history_and_clears_active_editor_claims():
    item = dict(compiling=True, pie=True, saving=True, blockers=["editor_busy"],
                state_sampled_at="last-sample", control_error=dict(code="instance_unresponsive"))
    exited(item)
    assert item["compiling"] is False and item["pie"] is False and item["saving"] is False
    assert item["blockers"] == []
    assert item["last_editor_snapshot"]["compiling"] is True
    assert item["last_editor_snapshot"]["state_sampled_at"] == "last-sample"
