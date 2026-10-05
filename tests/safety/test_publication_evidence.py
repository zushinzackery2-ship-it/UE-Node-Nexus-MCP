"""Missing phases, failed exit and stale candidates must block main publication."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.safety.publication.evidence import binding, verify
from ue_node_nexus_mcp.safety.publication.risk import reasons
from ue_node_nexus_mcp.transcode.errors import SyncError
from ue_node_nexus_mcp.transcode.storage.io import digest


def builds():
    row = dict(loaded=True, source_fingerprint="fingerprint", source_commit="commit", source_dirty=False,
               version="0.6.0", contract_version=4, build_id="engine")
    return dict((name, dict(row)) for name in ("UeNodeNexusBridge", "UeNodeNexusGuard", "UeNodeNexusVfxBridge"))


def environment():
    return dict(engine_version="5.5.4", engine_build="build", engine_modules="modules", project_definition="project",
                configuration=dict(), plugins=dict(Niagara="descriptor"), content_mounts=["Niagara"])


def fixture():
    item = dict(asset="/Game/Probe.Probe", kind="niagara_system", payload=dict(plan=[dict(op="ns_module_add")]), dependencies=[])
    operation = dict(operation="vfx_transcode_apply", payload=item["payload"])
    expected = binding(item, "candidate", dict(revisions=dict()), builds(), "d3d12", environment())
    publication = dict(compiled=True, saved=True, readback_verified=True)
    result = dict(ok=True, data=dict(state="validated", source_unchanged=True, operations_digest=digest([operation]),
        worker=dict(build=builds(), rhi="d3d12", environment=environment()), cleanup=dict(exit_confirmed=True, forced=False, exit_code=0),
        results=[dict(ok=True, data=dict(isolated_publication=publication))]))
    return item, expected, operation, result


def test_complete_candidate_evidence_is_accepted():
    _, expected, operation, result = fixture()
    assert verify(result, expected, operation)["readback_verified"]


@pytest.mark.parametrize("field", ["compiled", "saved", "readback_verified"])
def test_omitted_phase_blocks_publication(field):
    _, expected, operation, result = fixture()
    result["data"]["results"][0]["data"]["isolated_publication"][field] = False
    with pytest.raises(SyncError, match="omitted"):
        verify(result, expected, operation)


@pytest.mark.parametrize("code", [3, None])
def test_abnormal_or_unknown_exit_is_refused(code):
    _, expected, operation, result = fixture()
    result["data"]["cleanup"]["exit_code"] = code
    with pytest.raises(SyncError, match="normal worker exit"):
        verify(result, expected, operation)


def test_different_candidate_is_refused():
    _, expected, operation, result = fixture()
    operation = deepcopy(operation)
    operation["payload"]["plan"][0]["value"] = "changed"
    with pytest.raises(SyncError, match="different operation"):
        verify(result, expected, operation)


def test_loaded_build_drift_is_refused():
    _, expected, operation, result = fixture()
    result["data"]["worker"]["build"]["UeNodeNexusBridge"]["source_fingerprint"] = "older"
    with pytest.raises(SyncError, match="differs"):
        verify(result, expected, operation)


def test_risk_rules_target_engine_contracts():
    item, _, _, _ = fixture()
    assert reasons(item)
    assert not reasons(dict(item, empty=True))
    ordinary = dict(item, kind="material", payload=dict(plan=[dict(op="set_property", name="Roughness", value="0.5")]))
    custom = dict(item, kind="material", payload=dict(plan=[dict(op="create_node", node_class="MaterialExpressionCustom")]))
    assert not reasons(ordinary)
    assert reasons(custom)


def test_existing_custom_edits_and_dirty_niagara_are_isolated():
    item, _, _, _ = fixture()
    assert reasons(dict(item, payload=dict(plan=[]), empty=False))
    custom = dict(item, kind="material", node_classes=dict(c=["Custom"]),
                  payload=dict(plan=[dict(op="set_node_prop", id="c", name="Code", value="return 1;")]))
    assert reasons(custom) == ["material_rendering_contract"]
    custom["payload"]["plan"] = [dict(op="connect_pins", **dict((("from", "c"), ("to", "out"))))]
    assert reasons(custom)
    custom["payload"]["plan"] = [dict(op="set_node_position", id="c", x=10, y=20)]
    assert not reasons(custom)
