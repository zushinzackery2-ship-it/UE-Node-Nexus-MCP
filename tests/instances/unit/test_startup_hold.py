"""A call that needs a starting editor is held until the start ends, and says how it ended."""

import pytest

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.lifecycle.policy import Policy
from ue_node_nexus_mcp.instances.lifecycle.prompts import exited, observe
from ue_node_nexus_mcp.instances.session import startup
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.instances.tools.dispatch import execute
from tests.instances.support.sdk import LocalBroker

LOW_SPACE = dict(id=1, source="message_dialog", code="low_drive_space", title="Warning: Low Drive Space", buttons="ok",
                 resolution="auto_acknowledged", result="Ok", meaning="free space is below UE's recommendation; confirming only continues startup",
                 locations=[dict(role="user", path="C:/Users/Nexus/Documents/", free_mb=493, recommended_mb=1024, total_mb=102400)])
SAVE_PROMPT = dict(id=2, source="message_dialog", title="Save Content", message="Save changes?", buttons="yes_no")


@pytest.fixture
def held(lifecycle, monkeypatch):
    """A session whose hold advances the manager clock, and a way to script the editor."""
    service, platform, clock, project = lifecycle
    steps = []

    def pause(seconds):
        clock.advance(seconds)
        if steps:
            steps.pop(0)(platform)

    monkeypatch.setattr(startup, "pause", pause)
    session = EditorSession(LocalBroker(service, project.parent), str(project), startup_wait=30)
    yield session, platform, steps, clock
    session.close()


def launch_with(platform, **fields):
    original = platform.launch

    def launch(instance):
        identity = original(instance)
        platform.records[instance["instance_id"]].update(fields)
        return identity

    platform.launch = launch


def update(**fields):
    def step(platform):
        for record in platform.records.values():
            record.update(fields)
    return step


def test_an_executed_ensure_holds_until_the_editor_is_ready(held):
    session, platform, steps, clock = held
    launch_with(platform, ready=False)
    steps.extend([update(), update(ready=True)])

    result = session.ensure(dict(mode="reuse_or_start", dry_run=False))

    assert result["instance"]["state"] == "READY", result
    assert result["startup"]["outcome"] == "ready" and "next" not in result["startup"]
    assert session.current()["state"] == "READY"


def test_an_acknowledged_advisory_is_reported_with_the_ready_editor(held):
    session, platform, _, _ = held
    progress = dict(phase="loop_init_complete", launch_window="hidden", last_log=dict(text="LogInit: Display: Engine is initialized."))
    launch_with(platform, dialog_notices=[LOW_SPACE], startup_progress=progress)

    envelope = execute("bridge_instance_ensure", dict(mode="reuse_or_start", dry_run=False), session)

    assert envelope["ok"], envelope
    report = envelope["data"]["startup"]
    assert report["outcome"] == "ready"
    assert report["acknowledged_dialogs"][0]["locations"][0]["free_mb"] == 493
    assert report["progress"] == progress and envelope["data"]["instance"]["startup_progress"] == progress
    assert any("Low Drive Space" in warning for warning in envelope["warnings"]), envelope["warnings"]
    # A held status keeps the editor's own progress beside the verdict of the hold.
    status = session.status(dict(wait_seconds=1))
    assert status["startup"]["outcome"] == "ready" and status["startup_progress"] == progress


def test_a_prompt_waiting_for_the_user_ends_the_hold_and_names_the_prompt(held):
    session, platform, steps, clock = held
    launch_with(platform, ready=False, waiting_for_user=True, blocking_dialog=SAVE_PROMPT)

    waiting = session.ensure(dict(mode="reuse_or_start", dry_run=False))

    assert waiting["startup"]["outcome"] == "waiting_for_user", waiting["startup"]
    assert waiting["startup"]["blocking_dialog"]["title"] == "Save Content"
    assert "answer" in waiting["startup"]["next"]
    with pytest.raises(InstanceError) as blocked:
        session.reserve("asset_get")
    assert blocked.value.code == "waiting_for_user"
    assert blocked.value.details["startup"]["blocking_dialog"]["message"] == "Save changes?"

    answered = dict(SAVE_PROMPT, resolution="answered", result="Yes")
    update(ready=True, waiting_for_user=False, blocking_dialog=None, dialog_notices=[answered])(platform)
    clock.advance(5)
    resumed = session.ensure(dict(mode="reuse_or_start", dry_run=False))
    assert resumed["startup"]["outcome"] == "ready" and resumed["instance"]["waiting_for_user"] is False


def test_the_time_a_prompt_waits_is_not_counted_against_the_start(held):
    """A user who takes longer than the start wait to answer must still get the same editor."""
    session, platform, steps, clock = held
    launch_with(platform, ready=False, waiting_for_user=True, blocking_dialog=SAVE_PROMPT)
    assert session.ensure(dict(mode="reuse_or_start", dry_run=False))["startup"]["outcome"] == "waiting_for_user"
    clock.advance(Policy().startup_seconds * 3)
    assert session.status(dict(wait_seconds=0))["state"] == "STARTING"

    update(waiting_for_user=False, blocking_dialog=None)(platform)
    clock.advance(5)
    assert session.status(dict(wait_seconds=0))["state"] == "STARTING"
    update(ready=True)(platform)
    clock.advance(5)
    ready = session.ensure(dict(mode="reuse_or_start", dry_run=False))
    assert ready["startup"]["outcome"] == "ready" and ready["instance"]["prompt_seconds"] >= Policy().startup_seconds * 3


def test_an_editor_that_exits_during_startup_is_a_startup_failure(held):
    session, platform, steps, clock = held
    launch_with(platform, ready=False)
    steps.append(lambda fake: fake.records.clear())

    with pytest.raises(InstanceError) as failure:
        session.ensure(dict(mode="reuse_or_start", dry_run=False))

    assert failure.value.code == "startup_failed"
    assert failure.value.details["instance"]["state"] == "EXITED"


def test_an_exit_while_a_prompt_waits_says_which_prompt_it_was(held):
    session, platform, steps, clock = held
    launch_with(platform, ready=False, waiting_for_user=True, blocking_dialog=SAVE_PROMPT)
    assert session.ensure(dict(mode="reuse_or_start", dry_run=False))["startup"]["outcome"] == "waiting_for_user"
    platform.records.clear()
    clock.advance(5)

    status = session.status(dict(wait_seconds=5))

    assert status["state"] == "EXITED" and status["waiting_for_user"] is False
    assert status["startup"]["outcome"] == "exited"
    assert status["startup"]["exited_while_waiting"]["title"] == "Save Content"


def test_implicit_work_is_held_through_the_start_it_joins(held):
    session, platform, steps, clock = held
    launch_with(platform, ready=False)
    assert session.ensure(dict(mode="reuse_or_start", dry_run=False, wait_seconds=0))["startup"]["outcome"] == "starting"
    steps.extend([update(), update(ready=True)])

    scope = session.reserve("asset_get")

    assert scope.instance["state"] == "READY"
    scope.release()


def test_the_hold_rejects_an_unusable_wait():
    for value in (-1, 901, True, "5"):
        with pytest.raises(InstanceError) as failure:
            startup.budget(value)
        assert failure.value.code == "invalid_request", value
    assert startup.budget(None, 7.0) == 7.0


def test_a_hidden_native_dialog_is_a_prompt_and_an_answer_clears_it():
    record = dict(ready=False)
    rows = [dict(title="Missing Modules", visible=False, class_name="#32770", dialog=True, message="Rebuild now?"),
            dict(title="Default IME", visible=False, class_name="IME", dialog=False)]
    observe(record, rows, None)
    assert record["waiting_for_user"] is True and record["window_titles"] == []
    assert record["blocking_dialog"] == dict(source="window", title="Missing Modules", message="Rebuild now?", visible=False)

    observe(record, rows, dict(waiting_for_user=True, blocking_dialog=SAVE_PROMPT))
    assert record["blocking_dialog"]["title"] == "Save Content"

    editor = [dict(title="Demo - Unreal Editor", visible=True, class_name="UnrealWindow", dialog=False)]
    observe(record, editor, dict(waiting_for_user=False))
    assert record["waiting_for_user"] is False and record["blocking_dialog"] is None
    assert record["window_titles"] == ["Demo - Unreal Editor"]

    record.update(waiting_for_user=True, blocking_dialog=SAVE_PROMPT)
    exited(record)
    assert record["exited_while_waiting"] == SAVE_PROMPT and record["windows"] == [] and record["blocking_dialog"] is None
