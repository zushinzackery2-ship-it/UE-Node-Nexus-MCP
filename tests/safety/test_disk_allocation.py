"""Tiny files on exFAT and dirty package baselines consume real disk blocks."""

import pytest

from ue_node_nexus_mcp.safety.isolation import capacity


@pytest.mark.parametrize("unit", [4096, 262144])
def test_budget_counts_target_volume_allocation(tmp_path, monkeypatch, unit):
    files = [tmp_path / f"input-{index}.txt" for index in range(4)]
    for path in files:
        path.write_bytes(b"x")
    monkeypatch.setattr(capacity, "allocation_unit", lambda path: unit)
    budget = capacity.estimate(dict(files=files), tmp_path, 1.5, 1024)
    assert budget["input_allocation_bytes"] == 8 * unit
    assert budget["required_bytes"] == int(12 * unit) + 1024


def test_live_baseline_is_reserved_before_copy(tmp_path, monkeypatch):
    source = tmp_path / "asset"
    source.write_bytes(b"x" * 8193)
    monkeypatch.setattr(capacity, "allocation_unit", lambda path: 4096)
    baseline = dict(files=[dict(source=str(source), hash="verified")])
    budget = capacity.estimate(dict(files=[]), tmp_path, 1.5, 1024, baseline)
    assert budget["required_bytes"] == 4 * 4096 * 1.5 + 1024
