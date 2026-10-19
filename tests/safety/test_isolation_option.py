"""Isolation is opt-in: off by default, ``auto`` for classified risk, ``always`` for every unit."""

import pytest

from ue_node_nexus_mcp.safety.publication import gate, mode
from ue_node_nexus_mcp.transcode.collaboration.apply import units
from ue_node_nexus_mcp.transcode.collaboration.report import options as report_options
from ue_node_nexus_mcp.transcode.errors import SyncError


def unit(asset, dependencies=(), rendering=False, kind="material_function"):
    operation = dict(op="connect", to="WorldPositionOffset" if rendering else "BaseColor")
    return dict(asset=asset, kind=kind, dependencies=list(dependencies), empty=False,
                interface_changed=False, node_classes=dict(),
                payload=dict(asset_path=asset, kind=kind, plan=[operation]))


def batch(units_by_asset, order):
    return dict(order=list(order), units=dict(units_by_asset), errors=dict())


def admitted(monkeypatch, rows, options):
    calls = []

    def admit_batch(bridge, context, workspace, entries, candidate, observation, opts):
        calls.append([asset for asset, _ in entries])
        return dict()

    monkeypatch.setattr(gate, "admit_batch", admit_batch)
    units.collect_batch(None, None, None, "candidate", dict(), rows, options)
    return calls[0] if calls else []


RISKY = batch({
    "/Game/MF_Base": unit("/Game/MF_Base"),
    "/Game/M_WPO": unit("/Game/M_WPO", ["/Game/MF_Base"], rendering=True, kind="material"),
}, ["/Game/MF_Base", "/Game/M_WPO"])

LOW_RISK = batch({
    "/Game/MF_Plain": unit("/Game/MF_Plain"),
    "/Game/M_LowRisk": unit("/Game/M_LowRisk", ["/Game/MF_Plain"], kind="material"),
}, ["/Game/MF_Plain", "/Game/M_LowRisk"])


def test_default_mode_is_off():
    assert mode.isolation_mode(dict()) == "off"
    assert mode.isolation_mode(None) == "off"
    assert mode.isolation_mode(dict(isolation="auto")) == "auto"


def test_off_copies_nothing(monkeypatch):
    assert admitted(monkeypatch, RISKY, dict()) == []
    assert admitted(monkeypatch, RISKY, dict(isolation="off")) == []


def test_auto_copies_only_classified_risk(monkeypatch):
    assert admitted(monkeypatch, RISKY, dict(isolation="auto")) == ["/Game/MF_Base", "/Game/M_WPO"]
    assert admitted(monkeypatch, LOW_RISK, dict(isolation="auto")) == []


def test_always_copies_every_planned_unit(monkeypatch):
    assert admitted(monkeypatch, LOW_RISK, dict(isolation="always")) == ["/Game/MF_Plain", "/Game/M_LowRisk"]


def test_admit_returns_no_receipt_when_off():
    item = unit("/Game/M_WPO", ["/Game/MF_Base"], rendering=True, kind="material")
    assert gate.admit(None, None, None, item, "candidate", dict(), dict(isolation="off")) is None


@pytest.mark.parametrize("value", ["sometimes", True, 1])
def test_unsupported_isolation_values_are_rejected(value):
    with pytest.raises(SyncError) as error:
        report_options.validate_values("push", dict(isolation=value))
    assert error.value.code == "invalid_option"


def test_push_accepts_every_documented_mode():
    for value in ("off", "auto", "always"):
        report_options.validate("push", dict(isolation=value))


def test_ue_sync_advertises_the_option():
    from ue_node_nexus_mcp.transcode.sync.options import OPTION_KEYS

    assert "isolation" in OPTION_KEYS["push"]
    assert "isolation" in OPTION_KEYS["continue"]
