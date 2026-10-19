"""A risky unit is validated against the changed dependencies it compiles with."""

from ue_node_nexus_mcp.safety.publication import gate
from ue_node_nexus_mcp.transcode.collaboration.apply import units


def unit(asset, dependencies=(), rendering=False, kind="material_function"):
    operation = dict(op="connect", to="WorldPositionOffset" if rendering else "BaseColor")
    return dict(asset=asset, kind=kind, dependencies=list(dependencies), empty=False,
                interface_changed=False, node_classes=dict(),
                payload=dict(asset_path=asset, kind=kind, plan=[operation]))


def batch(units_by_asset, order):
    return dict(order=list(order), units=dict(units_by_asset), errors=dict())


def admitted(monkeypatch, rows):
    captured = []

    def admit_batch(bridge, context, workspace, entries, candidate, observation, options):
        captured.append([asset for asset, _ in entries])
        return dict()

    monkeypatch.setattr(gate, "admit_batch", admit_batch)
    units.collect_batch(None, None, None, "candidate", dict(), rows, dict(isolation="auto"))
    return captured[0] if captured else []


def test_changed_low_risk_dependencies_join_the_batch(monkeypatch):
    rows = batch({
        "/Game/MF_Base": unit("/Game/MF_Base"),
        "/Game/MF_Mask": unit("/Game/MF_Mask", ["/Game/MF_Base"]),
        "/Game/M_WPO": unit("/Game/M_WPO", ["/Game/MF_Mask"], rendering=True, kind="material"),
        "/Game/M_Unrelated": unit("/Game/M_Unrelated"),
    }, ["/Game/MF_Base", "/Game/MF_Mask", "/Game/M_WPO", "/Game/M_Unrelated"])
    assert admitted(monkeypatch, rows) == ["/Game/MF_Base", "/Game/MF_Mask", "/Game/M_WPO"]


def test_unchanged_and_engine_dependencies_stay_in_the_baseline(monkeypatch):
    unchanged = unit("/Game/MF_Unchanged")
    unchanged["empty"] = True
    rows = batch({
        "/Game/MF_Unchanged": unchanged,
        "/Game/M_WPO": unit("/Game/M_WPO", ["/Game/MF_Unchanged", "/Game/MF_Stable",
                                          "/Engine/EngineMaterials/DefaultMaterial"], rendering=True, kind="material"),
    }, ["/Game/MF_Unchanged", "/Game/M_WPO"])
    assert admitted(monkeypatch, rows) == ["/Game/M_WPO"]


def test_dependencies_outside_the_batch_are_not_admitted(monkeypatch):
    rows = batch({
        "/Game/MF_Risky": unit("/Game/MF_Risky", ["/Game/Missing"], rendering=True, kind="material"),
    }, ["/Game/MF_Risky"])
    assert admitted(monkeypatch, rows) == ["/Game/MF_Risky"]


def test_batches_without_risk_start_no_worker(monkeypatch):
    rows = batch({
        "/Game/MF_Plain": unit("/Game/MF_Plain"),
        "/Game/M_LowRisk": unit("/Game/M_LowRisk", ["/Game/MF_Plain"], kind="material"),
    }, ["/Game/MF_Plain", "/Game/M_LowRisk"])
    assert admitted(monkeypatch, rows) == []
