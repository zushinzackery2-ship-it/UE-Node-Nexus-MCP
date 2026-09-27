"""One isolated lifecycle per tier; no test depends on another test's state.

UE_NEXUS_PERF_SCALE selects tiers, default 1000; 1000,10000 runs both. Counts
guard work proportional to the change. Timings are reported for investigation;
filesystem scheduling is not a product contract. Memory retains the original
8 KiB idle / 32 KiB publication budget per asset.
"""

import os

import pytest

from .fixtures import EDITED, FANOUT, Project
from .measurement import encodes, locks, peak_bytes, report

TIERS = tuple(int(item) for item in os.environ.get("UE_NEXUS_PERF_SCALE", "1000").split(","))


@pytest.fixture(params=TIERS, ids=[f"{scale}-entities" for scale in TIERS])
def project(request, tmp_path):
    return Project(request.param, tmp_path)


def check_worktree_cache(project):
    before = project.counted()
    observed = project.timed("fetch", "fetch")
    assert project.counted() == before
    assert len(observed["revisions"]) == project.entities
    with encodes() as calls:
        project.timed("status-cold", "status")
        assert len(calls) == project.entities
        calls.clear()
        project.timed("status", "status")
        assert calls == []
        project.edit(project.materials[0])
        changed = project.timed("status-edited", "status")
        assert len(calls) == 1
        assert changed["unstaged"] == [project.materials[0]]
        calls.clear()
        project.edit(project.materials[0], *reversed(EDITED))
        restored = project.timed("status-restored", "status")
        assert len(calls) == 1
        assert restored["dirty"] is False


def check_publication(project):
    project.edit(project.materials[0])
    exports, applies = project.counted()
    with locks() as records:
        project.timed("stage", "stage")
        project.timed("commit", "commit", all=True, message="one edit")
        assert not any(row[0] == "publication.lock" for row in records)
        records.clear()
        result = project.timed("push", "push")
        assert sum(row[0] == "publication.lock" for row in records) == 1
    assert result["status"] == "published", result
    assert [row["action"] for row in result["rows"]] == ["pushed"]
    assert project.counted() == (exports + 1 + FANOUT, applies + 1)


def check_history_and_memory(project):
    # The same edited asset moves through five commits without publishing.
    # Idle inspection must still use the memo, and publication the final edit.
    for number in range(5):
        project.edit(project.materials[1], *(EDITED if number % 2 == 0 else EDITED[::-1]))
        project.run("commit", all=True, message=f"round {number}")
    before = project.counted()
    with encodes() as calls:
        project.timed("status-deep-first", "status")
        assert len(calls) <= 1
        calls.clear()
        for _ in range(2):
            project.timed("status-deep", "status")
        assert calls == []
    assert project.counted() == before
    idle, published = [], []
    with peak_bytes(idle):
        project.run("status")
    with peak_bytes(published):
        result = project.timed("push-traced", "push")
    assert result["status"] == "published", result
    assert result["applied"] == 1
    assert project.counted() == (before[0] + 1 + FANOUT, before[1] + 1)
    assert idle[0] / project.entities < 8 * 1024
    assert published[0] / project.entities < 32 * 1024
    return dict(idle_peak_bytes=idle[0], publication_peak_bytes=published[0])


def test_workspace_work_and_memory_budgets(project, record_testsuite_property):
    assert len(project.ue.assets) == project.entities
    assert sorted(project.ue.exported) == sorted(project.ue.assets)
    assert project.ue.export_calls == 1
    check_worktree_cache(project)
    check_publication(project)
    peaks = check_history_and_memory(project)
    report(project, peaks, record_testsuite_property)
