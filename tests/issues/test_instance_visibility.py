"""Issues 6, 7, 8 and 16: what a reader is told about an editor it may pin.

A read that reports readiness a writer would not accept, a pin that refuses the
one window worth protecting, and an error that names no field are all the same
defect seen from different sides: the caller cannot act on what it was told.
"""

from __future__ import annotations

import time

import pytest

from tests.instances.unit.conftest import Client, lifecycle  # noqa: F401


def started(service, project):
    """One instance that has been launched but never observed as ready."""
    client = Client(service)
    result = client.require("ensure", project_path=str(project), mode="reuse_or_start", dry_run=False)
    return client, result["instance"]["instance_id"]


def test_status_refreshes_a_stale_snapshot_instead_of_reporting_the_old_state(lifecycle):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, instance_id = started(service, project)

    # The launcher answered STARTING; the process is ready and no sweep has run.
    with service.lock:
        assert service.instances[instance_id]["state"] == "STARTING"
    clock.advance(service.policy.sweep_seconds + 1)
    assert client.require("status", instance_id=instance_id)["state"] in ("READY", "IDLE")


def test_a_pin_holds_a_cold_starting_editor(lifecycle):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, instance_id = started(service, project)
    with service.lock:
        service.instances[instance_id]["state"] = "STARTING"

    result = client.require("pin", instance_id=instance_id, seconds=60, reason="cold start", dry_run=False)
    assert result["state"] == "STARTING"
    assert result["pin_seconds"] == 60 and result["renewable"] is True
    assert result["max_pin_seconds"] == service.policy.max_pin_seconds
    with service.lock:
        assert service.instances[instance_id]["pin_until"] == clock() + 60


def test_pin_reports_the_field_it_rejected_and_refuses_fields_it_does_not_know(lifecycle):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, instance_id = started(service, project)

    missing = client.call("pin", instance_id=instance_id, reason="hold", dry_run=False)["error"]
    assert missing["code"] == "invalid_pin"
    assert missing["details"]["field"] == "seconds"
    assert missing["details"]["max_seconds"] == service.policy.max_pin_seconds

    over = client.call("pin", instance_id=instance_id, seconds=service.policy.max_pin_seconds + 1,
                       reason="hold", dry_run=False)["error"]
    assert over["code"] == "invalid_pin" and over["details"]["field"] == "seconds"

    unknown = client.call("pin", instance_id=instance_id, seconds=60, reason="hold",
                          duration=60, dry_run=False)["error"]
    assert unknown["code"] == "invalid_pin"
    assert unknown["details"]["unsupported"] == ["duration"]
    assert "seconds" in unknown["details"]["fields"]


@pytest.mark.parametrize("field", ["snapshot_age_seconds", "stale"])
def test_every_listed_row_says_how_old_its_evidence_is(lifecycle, field):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, _ = started(service, project)
    rows = client.require("list", project_path=str(project))["instances"]
    assert rows and all(field in row for row in rows)


def test_a_row_whose_snapshot_aged_out_stops_claiming_readiness(lifecycle, monkeypatch):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, instance_id = started(service, project)
    service.reconcile()
    fresh = next(row for row in client.require("list")["instances"] if row["instance_id"] == instance_id)
    assert fresh["stale"] is False and fresh["ready"] is True

    # Age the observation past the freshness window without touching the clock
    # the sweep uses, so the row is reported from evidence and nothing else.
    limit = max(service.policy.suspect_seconds, service.policy.sweep_seconds * 3)
    service.editors.seen[instance_id] -= limit + 5
    monkeypatch.setattr(service.editors, "observe", lambda: None)

    stale = next(row for row in client.require("list")["instances"] if row["instance_id"] == instance_id)
    assert stale["stale"] is True and stale["ready"] is False
    assert stale["snapshot_age_seconds"] > limit
    assert stale["error"]["code"] == "snapshot_stale"


def test_an_instance_never_observed_is_reported_as_unmeasured_rather_than_ready(lifecycle):  # noqa: F811
    service, platform, clock, project = lifecycle
    client, instance_id = started(service, project)
    service.editors.seen.pop(instance_id, None)

    with service.lock:
        row = service.editors.row(service.instances[instance_id])
    assert row["stale"] is True and row["ready"] is False and row["snapshot_age_seconds"] is None
