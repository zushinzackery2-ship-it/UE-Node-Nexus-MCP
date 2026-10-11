"""Expiration, shared capacity and paging evidence survive cache turnover."""

from concurrent.futures import ThreadPoolExecutor

import pytest

from ue_node_nexus_mcp.facade_state import FacadeState
from ue_node_nexus_mcp.instances.errors import InstanceError


def test_ttl_is_not_extended_by_access_and_covers_both_kinds():
    now = [0.0]
    state = FacadeState(ttl_seconds=10, clock=lambda: now[0])
    artifact = state.store_artifact("test", dict(value=1))
    diff = state.store_diff("test", dict(), dict(ok=True))
    now[0] = 9.0
    assert state.get_artifact(artifact.artifact_id) is artifact
    now[0] = 10.0
    assert state.get_artifact(artifact.artifact_id) is None
    assert state.get_diff(diff.diff_token) is None
    assert state.statistics()["entries"] == 0


def test_artifacts_and_diffs_share_lru_capacity():
    state = FacadeState(max_entries=3)
    first = state.store_artifact("test", dict(value=1))
    evicted = state.store_diff("test", dict(), dict(ok=True))
    third = state.store_artifact("test", dict(value=3))
    assert state.get_artifact(first.artifact_id) is first
    last = state.store_diff("test", dict(), dict(ok=True))
    assert state.get_diff(evicted.diff_token) is None
    assert state.get_artifact(first.artifact_id) is first
    assert state.get_artifact(third.artifact_id) is third
    assert state.get_diff(last.diff_token) is last
    assert state.statistics()["entries"] == 3


def test_retained_bytes_are_bounded_including_encoded_artifact():
    state = FacadeState(max_bytes=20000)
    first = state.store_artifact("test", dict(value="x" * 8000))
    second = state.store_artifact("test", dict(value="y" * 8000))
    assert state.get_artifact(first.artifact_id) is None
    assert state.get_artifact(second.artifact_id) is second
    assert second.encoded is not None
    assert state.statistics()["retained_bytes"] <= 20000


def test_oversized_record_is_explicitly_rejected_without_evicting_existing():
    state = FacadeState(max_bytes=6000)
    previous = state.store_artifact("test", dict(value="small"))
    with pytest.raises(InstanceError) as error:
        state.store_artifact("test", dict(value="x" * 8000))
    assert error.value.code == "facade_cache_capacity_exceeded"
    assert state.get_artifact(previous.artifact_id) is previous


def test_concurrent_turnover_preserves_capacity_and_records():
    state = FacadeState(max_entries=32)

    def work(index):
        record = state.store_artifact("test", dict(value=index))
        found = state.get_artifact(record.artifact_id)
        assert found is None or found.payload["value"] == index

    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(work, range(160)))
    assert state.statistics()["entries"] <= 32
