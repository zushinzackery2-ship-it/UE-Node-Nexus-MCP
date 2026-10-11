"""A prompt opened after startup has priority over remembered READY state."""

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.startup import hold, outcome, require
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from tests.instances.support.sdk import LocalBroker


def test_ready_modal_is_polled_and_returns_the_current_prompt():
    modal = dict(instance_id="one", state="READY", ready=False, waiting_for_user=True,
                 blocking_dialog=dict(title="Save changes", message="Choose an action", buttons=["Save", "Cancel"]))
    assert outcome(modal) == "waiting_for_user"
    polls = []
    row, report = hold(lambda key: (polls.append(key), modal)[-1], modal, 0)
    assert polls == ["one"]
    with pytest.raises(InstanceError) as failure:
        require(row, report)
    assert failure.value.code == "waiting_for_user"
    assert failure.value.details["startup"]["blocking_dialog"] == modal["blocking_dialog"]
    ready = dict(modal, ready=True, waiting_for_user=False, blocking_dialog=None)
    row, report = hold(lambda _: ready, modal, 0)
    assert report["outcome"] == "ready"


def test_broker_ready_editor_becomes_blocked_until_modal_clears(lifecycle, monkeypatch):
    service, platform, clock, project = lifecycle
    monkeypatch.setattr("ue_node_nexus_mcp.instances.session.startup.pause", clock.advance)
    session = EditorSession(LocalBroker(service, project.parent), str(project))
    try:
        started = session.ensure(dict(mode="reuse_or_start", dry_run=False))
        identifier = started["instance"]["instance_id"]
        assert started["startup"]["outcome"] == "ready"
        prompt = dict(title="Save changes", message="Choose an action", buttons="yes_no")
        platform.records[identifier].update(ready=False, waiting_for_user=True, blocking_dialog=prompt)
        service.editors.refresh()
        observed = session.status(dict(fresh=True, wait_seconds=0))
        assert observed["state"] == "BLOCKED"
        assert observed["startup"]["outcome"] == "waiting_for_user"
        assert observed["startup"]["blocking_dialog"] == prompt
        platform.records[identifier].update(ready=True, waiting_for_user=False, blocking_dialog=None)
        service.editors.refresh()
        observed = session.status(dict(fresh=True, wait_seconds=0))
        assert observed["state"] == "READY" and observed["startup"]["outcome"] == "ready"
    finally:
        session.close()
