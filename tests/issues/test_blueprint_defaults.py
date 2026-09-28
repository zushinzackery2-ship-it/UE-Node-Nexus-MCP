"""Issues3 #4: native zero values and misplaced graph input parameters."""

from copy import deepcopy

import pytest

from ue_node_nexus_mcp.transcode.bp_types import parse_type_text
from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import actual_snapshot
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.sync_project import SyncError
from tests.issues.test_published_semantics import receipt_workspace, self_graph_raw


@pytest.mark.parametrize("asset_class", ("Blueprint", "AnimBlueprint"))
@pytest.mark.parametrize("kind,authored,native,changed", (
    ("float", "0", "0.000000", "1"),
    ("double", "0.0", "0", "5"),
    ("bool", "false", "False", "True"),
    ("int", "0", "0", "10")))
def test_zero_variable_readback_retains_mismatch_detection(
        tmp_path, asset_class, kind, authored, native, changed):
    raw = self_graph_raw()
    raw["class_short"] = asset_class
    raw["blueprint"] = dict(graphs=[], variables=[dict(
        name="Value", guid="VALUE", type=parse_type_text(kind), default=native,
        flags=["Transient"])])
    candidate = capture(
        f"nexus: 1\nasset: /Game/BP_Test\nclass: {asset_class}\nschema: key\n"
        f"[variables]\nValue : {kind} = {authored} {{Transient}}\n",
        None, "author", "blueprint")
    workspace, commit, asset = receipt_workspace(tmp_path, candidate)
    record = dict(id="apply", candidate=commit, asset=asset,
                  request=dict(plan=[dict(op="bp_variable_add", name="Value")]),
                  receipt=dict(after=raw, response_data=dict(id_map=dict())))
    assert actual_snapshot(workspace, record)
    altered = deepcopy(record)
    altered["receipt"]["after"]["blueprint"]["variables"][0]["default"] = changed
    with pytest.raises(SyncError) as error:
        actual_snapshot(workspace, altered)
    assert error.value.code == "apply_result_mismatch"


def test_graph_property_block_has_a_source_diagnostic():
    document, parsed = parse(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
        "[graph EventGraph]\n"
        "moving : CallFunction(KismetMathLibrary.Greater_DoubleDouble) @ 0,0 {B=10}\n")
    assert not parsed.errors()
    errors = lint_document(document, "blueprint", None).errors()
    assert [item.code for item in errors] == ["unsupported_node_properties"]
    assert errors[0].line == 6


def test_graph_named_input_argument_is_supported():
    document, parsed = parse(
        "nexus: 1\nasset: /Game/BP_Test\nclass: Blueprint\nschema: key\n"
        "[graph EventGraph]\n"
        "moving : CallFunction(KismetMathLibrary.Greater_DoubleDouble, B=10) @ 0,0\n")
    assert not parsed.errors()
    assert not lint_document(document, "blueprint", None).errors()
