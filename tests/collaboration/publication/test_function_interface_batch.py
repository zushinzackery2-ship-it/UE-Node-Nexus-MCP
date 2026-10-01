"""Issue 4 #3: a function interface change and its callers' wiring publish as one batch.

Refreshing a caller on its own right after the function changed compiled a graph
that lacked the wiring the same batch was about to add, rolled the caller back and
reported the failure against the function. A caller in the batch now rebuilds its
calls inside its own apply; any other caller is refreshed alone and owns its outcome.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply import refresh
from ue_node_nexus_mcp.transcode.material.calls import REFRESH, with_refresh
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.sync.service import run_sync
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_function_raw, material_raw, prop

FUNCTION = "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S"
MATERIAL = "/Game/Materials/M_Glass.M_Glass"
SECOND = "/Game/Materials/M_Rim.M_Rim"
CALL_CLASS = "/Script/Engine.MaterialExpressionMaterialFunctionCall"


def package(asset: str) -> str:
    return asset.split(".", 1)[0]


@pytest.fixture
def project(tmp_path):
    ue = ProtocolUe()
    ue.assets[FUNCTION] = material_function_raw()
    ue.assets[MATERIAL]["graph"]["nodes"].append(dict(
        guid="G-CALL", name="Call", class_short="MaterialFunctionCall", x=-600, y=0,
        inputs=["A", "B"], outputs=["Result"], props=[prop("MaterialFunction", "UMaterialFunctionInterface*", FUNCTION, "None")],
        **{"class": CALL_CLASS}))
    second = material_raw()
    second["asset_path"] = SECOND
    ue.assets[SECOND] = second
    root = tmp_path / "decoded"
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=dict(UE_NEXUS_TRANSCODE_DIR=str(root)))
    record = dict(path=CALL_CLASS, props=dict(MaterialFunction=dict(type="object", kind="object", default="None")),
                  inputs=["A", "B"], outputs=["Result"], dynamic_pins=True)
    publish(root / ".nexus" / "schema" / SCHEMA_KEY, SCHEMA_KEY, dict(material_expression={CALL_CLASS: record}), incremental=True)
    return ue, dict(UE_NEXUS_TRANSCODE_DIR=str(root)), workspace


def call(project, action, paths=None, **options):
    ue, env, workspace = project
    try:
        return run_sync(ue, action, paths, dict(dry_run=False, workspace_id=workspace["id"], **options), env=env)
    except SyncError as exc:
        pytest.fail(f"{exc.code}: {exc.details}")


def edit(project, asset, old, new):
    path = Path(project[2]["file_paths"][asset])
    text = path.read_text(encoding="utf-8")
    assert old in text, (asset, text)
    path.write_text(text.replace(old, new), encoding="utf-8")


def rename_input(project):
    """``B`` becomes ``Bee``: every call node of the function has to be rebuilt."""
    edit(project, FUNCTION, "InputName=B,", "InputName=Bee,")


def test_a_caller_in_the_same_batch_rebuilds_its_calls_inside_its_own_apply(project):
    ue, env, workspace = project
    ue.referencers[FUNCTION] = [package(MATERIAL), package(SECOND)]
    rename_input(project)
    edit(project, MATERIAL, "constant -> add.B\n", "constant -> add.B\nconstant -> fn_ws_s.Bee\n")
    call(project, "commit", all=True, message="new input and its wiring")

    result = call(project, "push", [FUNCTION, MATERIAL, SECOND])

    assert result["status"] == "published", result
    assert [payload["asset_path"] for payload in ue.applied] == [FUNCTION, SECOND, MATERIAL]
    # The caller outside the batch is rebuilt alone; the one inside rebuilds before its new wiring.
    assert [verb["op"] for verb in ue.applied[1]["plan"]] == [REFRESH]
    ops = [verb["op"] for verb in ue.applied[2]["plan"]]
    assert ops.index(REFRESH) < ops.index("connect_pins"), ops
    rows = dict((row["asset"], row) for row in result["rows"])
    assert rows[MATERIAL]["action"] == "pushed" and rows[MATERIAL]["refreshed"] == [FUNCTION]
    assert ue.assets[MATERIAL]["refreshed"] == [FUNCTION] and ue.assets[SECOND]["refreshed"] == [FUNCTION]


def test_a_selected_unchanged_caller_owns_its_refresh_failure(project):
    ue, env, workspace = project
    ue.referencers[FUNCTION] = [package(SECOND)]
    ue.fail_save = set((SECOND,))
    rename_input(project)
    call(project, "commit", all=True, message="new input")

    result = call(project, "push", [FUNCTION, SECOND])

    assert result["status"] == "partial", result
    assert FUNCTION not in result["errors"]
    failure = result["errors"][SECOND]
    assert failure["function"] == FUNCTION and failure["code"] == "apply_rolled_back", failure
    rows = dict((row["asset"], row) for row in result["rows"])
    assert rows[FUNCTION]["action"] == "pushed" and rows[SECOND]["action"] == "failed"
    assert "refreshed" not in ue.assets[SECOND]


def test_a_caller_left_on_the_old_interface_is_rebuilt_by_its_own_fix(project):
    """The failure names the caller, so fixing the caller's wiring must be enough to publish it."""
    ue, env, workspace = project
    ue.referencers[FUNCTION] = [package(MATERIAL)]
    ue.fail_save = set((MATERIAL,))
    rename_input(project)
    call(project, "commit", all=True, message="new input")
    assert MATERIAL in call(project, "push", [FUNCTION, MATERIAL])["errors"]
    assert ue.assets[MATERIAL]["graph"]["nodes"][-1]["inputs"] == ["A", "B"]

    ue.fail_save = False
    edit(project, MATERIAL, "constant -> add.B\n", "constant -> add.B\nconstant -> fn_ws_s.Bee\n")
    call(project, "commit", all=True, message="wire the new input")
    before = len(ue.applied)
    result = call(project, "push", [MATERIAL])

    assert result["status"] == "published", result
    ops = [verb["op"] for verb in ue.applied[before]["plan"]]
    assert ops == [REFRESH, "connect_pins"], ops
    node = ue.assets[MATERIAL]["graph"]["nodes"][-1]
    assert node["inputs"] == ["A", "Bee"]
    assert any(link["to"] == node["guid"] and link["to_in"] == 1 for link in ue.assets[MATERIAL]["graph"]["links"])


def test_an_unchanged_selected_caller_keeps_refresh_failure_context(project):
    ue, _, _ = project
    ue.referencers[FUNCTION] = [package(MATERIAL)]
    ue.fail_save = set((MATERIAL,))
    rename_input(project)
    call(project, "commit", all=True, message="interface only in a full workspace")
    result = call(project, "push")
    failure = result["errors"][MATERIAL]
    assert failure["asset"] == MATERIAL and failure["function"] == FUNCTION
    assert failure["details"]["stage"] == "refresh_function_calls"
    assert FUNCTION not in result["errors"]


def test_a_rebuild_is_requested_once_per_function():
    plan = [dict(op="disconnect_pins", to="call"), dict(op=REFRESH, function=FUNCTION), dict(op="connect_pins", to="call")]

    assert with_refresh(plan, set((FUNCTION,))) == plan
    other = "/Game/Functions/MF_Other.MF_Other"
    assert [op.get("function") for op in with_refresh(plan, set((FUNCTION, other)))] == [None, FUNCTION, other, None]


def test_only_a_unit_that_calls_the_function_itself_is_left_to_its_own_apply(monkeypatch):
    """A settled unit carries no document: it cannot rebuild anything when its turn comes."""
    refreshed = []
    monkeypatch.setattr(refresh, "measure", lambda *args, **kwargs: dict())
    monkeypatch.setattr(refresh, "consumer", lambda bridge, context, workspace, asset, functions, *rest: refreshed.append(asset))
    registry = [[package(MATERIAL), "hard"], [package(SECOND), "hard"]]

    def bridge(operation, payload):
        assert operation == "asset_referencers_get" and payload["asset_path"] == FUNCTION
        return dict(ok=True, data=dict(items=registry))

    batch = dict(errors=dict(), selected=[MATERIAL, SECOND], units={
        MATERIAL: dict(asset=MATERIAL, kind="material", empty=False, dependencies=[FUNCTION]),
        SECOND: dict(asset=SECOND, kind="", empty=True, dependencies=[]),
    })

    failures = refresh.callers(bridge, None, None, FUNCTION, "source", dict(), batch, dict(), set((MATERIAL, SECOND)))

    assert failures == [] and refreshed == [SECOND]


def test_new_output_survives_replanning_against_a_stale_catalog(project):
    ue, env, workspace = project
    ue.referencers[FUNCTION] = [package(MATERIAL)]
    catalog = Path(env["UE_NEXUS_TRANSCODE_DIR"]) / ".nexus/schema" / SCHEMA_KEY
    publish(catalog, SCHEMA_KEY, dict(material_functions={
        FUNCTION: dict(inputs=[dict(name="A"), dict(name="B")], outputs=[dict(name="Result")])
    }, material_expression={"MaterialExpressionFunctionOutput": dict(
        path="/Script/Engine.MaterialExpressionFunctionOutput", inputs=[""], outputs=[],
        props=dict(OutputName=dict(type="name", default="Result"), SortPriority=dict(type="number", default="0")))
    }), incremental=True)
    file = Path(workspace["file_paths"][FUNCTION])
    file.write_text(file.read_text(encoding="utf-8") + "\nout_skirt : FunctionOutput(OutputName=Skirt, SortPriority=7)\nin_a -> out_skirt\n", encoding="utf-8")
    edit(project, MATERIAL, "constant -> add.B\n", "constant -> add.B\nfn_ws_s.Skirt -> out.Roughness\n")
    call(project, "commit", all=True, message="new output and caller wiring")

    def generated_caller_change():
        next(row for row in ue.assets[MATERIAL]["props"] if row["name"] == "TwoSided")["value"] = "False"

    ue.after_apply = generated_caller_change
    result = call(project, "push", [FUNCTION, MATERIAL])
    assert result["status"] == "published", result.get("errors", result)
    assert not result["errors"]
    caller = ue.assets[MATERIAL]["graph"]["nodes"][-1]
    assert caller["outputs"] == ["Result", "Skirt"]


def test_partial_function_push_does_not_save_unselected_callers(project):
    ue, _, workspace = project
    ue.referencers[FUNCTION] = [package(MATERIAL), package(SECOND)]
    rename_input(project)
    edit(project, MATERIAL, "constant -> add.B\n", "constant -> add.B\nconstant -> fn_ws_s.Bee\n")
    call(project, "commit", all=True, message="interface and downstream intent")
    result = call(project, "push", [FUNCTION])
    assert result["status"] == "published", result
    assert [payload["asset_path"] for payload in ue.applied] == [FUNCTION]
    assert set(result["deferred_callers"]) == set((MATERIAL, SECOND))
    result = call(project, "push", [MATERIAL])
    assert result["status"] == "published", result
    assert "Bee" in ue.assets[MATERIAL]["graph"]["nodes"][-1]["inputs"]
