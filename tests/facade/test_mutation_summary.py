"""Compact receipts describe observed mutations, including partial deletion."""

from copy import deepcopy

import pytest

from tests.support.bridges import RoutingBridge
from ue_node_nexus_mcp.server import ue_execute

ASSETS = ["/Game/A.A", "/Game/B.B", "/Game/C.C"]


def deletion_response(deleted, *, ok=True, dry_run=False, changed=None):
    remaining = [asset for asset in ASSETS if asset not in deleted]
    item = dict(order=ASSETS, packages_on_disk=[asset.split(".")[0] for asset in remaining], residual_packages=[])
    data = dict(dry_run=dry_run, applied=ok and not dry_run, changed=bool(deleted) if changed is None else changed,
                deleted_objects=len(deleted), requested=3, deleted_assets=deleted, remaining_assets=remaining,
                diff=dict(assets_deleted=[item]))
    result = dict(ok=ok, data=data)
    if not ok:
        result["error"] = dict(code="asset_delete_failed", message="some targets remain")
    return result


@pytest.mark.parametrize("mode", ["delta", "summary", "brief", "ids_only", "silent"])
def test_three_deleted_assets_remain_visible_in_every_compact_mode(all_features, use_bridge, mode):
    use_bridge(RoutingBridge(dict(asset_delete_batch=deletion_response(ASSETS))))
    result = ue_execute("asset_delete_batch", dict(asset_paths=ASSETS, dry_run=False), response=dict(mode=mode))
    assert result["ok"] and result["data"]["changed"]
    assert result["data"]["affected"]["assets"] == ASSETS
    assert result["data"]["changes"]["deleted_assets"] == ASSETS
    assert result["data"]["created_ids"] == []
    assert result["data"]["remaining_assets"] == result["data"]["packages_on_disk"] == []


@pytest.mark.parametrize("case", ["no_change", "dry_run", "rejected", "partial"])
def test_delete_summary_uses_actual_results_instead_of_the_requested_set(all_features, use_bridge, case):
    deleted = ASSETS[:1] if case == "partial" else []
    raw = deletion_response(deleted, ok=case not in ("rejected", "partial"),
                            dry_run=case == "dry_run", changed=case in ("dry_run", "partial"))
    use_bridge(RoutingBridge(dict(asset_delete_batch=raw)))
    result = ue_execute("asset_delete_batch", dict(asset_paths=ASSETS, dry_run=case == "dry_run"))
    assert result["data"]["changed"] == (case == "partial")
    assert result["data"]["changes"]["deleted_assets"] == deleted
    assert result["data"]["affected"]["assets"] == deleted
    assert result["data"]["created_ids"] == []
    assert result["data"]["remaining_assets"] == [asset for asset in ASSETS if asset not in deleted]
    assert result["ok"] == (case not in ("rejected", "partial"))


def test_a_read_response_cannot_claim_creation_from_its_asset_path(all_features, use_bridge):
    use_bridge(RoutingBridge(dict(asset_get=dict(ok=True, data=dict(asset_path=ASSETS[0])))))
    result = ue_execute("asset_get", dict(asset_path=ASSETS[0]), response=dict(mode="delta"))
    assert result["data"]["changed"] is False
    assert result["data"]["created_ids"] == []
    assert result["data"]["affected"]["assets"] == []


@pytest.mark.parametrize("mode", ["full", "debug"])
def test_large_deletion_receipt_keeps_mutation_evidence_beside_artifact(all_features, use_bridge, mode):
    raw = deletion_response(ASSETS)
    raw["data"]["log"] = "evidence " * 3000
    use_bridge(RoutingBridge(dict(asset_delete_batch=deepcopy(raw))))
    result = ue_execute("asset_delete_batch", dict(asset_paths=ASSETS, dry_run=False), response=dict(mode=mode))
    assert result["data"]["stored_as_artifact"]
    assert result["data"]["changed"] and result["data"]["changes"]["deleted_assets"] == ASSETS
    assert result["data"]["affected"]["assets"] == ASSETS


@pytest.mark.parametrize("field,change,key", [("assets_created", "created_assets", "asset_path"),
                                               ("params_changed", "modified_assets", "asset_path")])
def test_native_created_and_modified_sets_stay_distinct(all_features, use_bridge, field, change, key):
    raw = dict(ok=True, data=dict(changed=True, applied=True, dry_run=False, diff={field: [{key: ASSETS[0]}]}))
    use_bridge(RoutingBridge(dict(asset_create=raw)))
    result = ue_execute("asset_create", dict(asset_path=ASSETS[0], asset_kind="material", dry_run=False))
    assert result["data"]["changes"][change] == ASSETS[:1]
    assert result["data"]["changes"]["deleted_assets"] == []
    assert result["data"]["created_ids"] == (ASSETS[:1] if change == "created_assets" else [])
