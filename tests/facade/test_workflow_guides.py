"""Workflow guide operation tests on the live ue_execute facade path."""

from __future__ import annotations

from pathlib import Path

import pytest

from ue_node_nexus_mcp.tools_facade import ue_execute
from ue_node_nexus_mcp.facade_capabilities import ue_capability_get, ue_context_get
from ue_node_nexus_mcp.workflow_guides import GUIDE_INDEX

GUIDES_DIR = Path(__file__).resolve().parents[2] / "src/ue_node_nexus_mcp/guides"


def test_listing_categories_through_ue_execute(all_features: None) -> None:
    result = ue_execute("workflow_guide_get", {})
    assert result["ok"] is True
    categories = {entry["category"] for entry in result["data"]["categories"]}
    assert categories == set(GUIDE_INDEX)
    assert categories == set(path.stem for path in GUIDES_DIR.glob("*.md"))
    assert result["data"]["usage"]["get_one"] == {"category": "getting_started"}
    for category in sorted(categories):
        guide = ue_execute("workflow_guide_get", dict(category=category))
        assert guide["ok"] is True, category
        assert guide["data"]["category"] == category
        data = guide["data"]
        if data.get("stored_as_artifact"):
            from ue_node_nexus_mcp.server import ue_read

            page = ue_read("artifact", query=dict(artifact_id=data["artifact"]["id"], path="data.body"))
            assert page["ok"]
            assert page["data"]["value"].strip(), category
        else:
            assert data["body"].strip(), category


def test_keyword_query_routes_to_the_right_guide(all_features: None) -> None:
    result = ue_execute("workflow_guide_get", {"query": "how do I connect pins on a graph"})
    assert result["ok"] is True
    matches = result["data"]["matches"]
    assert matches and matches[0]["category"] == "graph_editing"
    assert result["data"]["best"]["category"] == "graph_editing"
    assert "connect_pins" in result["data"]["best"]["body"]


def test_unmatched_query_returns_category_list(all_features: None) -> None:
    result = ue_execute("workflow_guide_get", {"query": "zzz nothing relevant"})
    assert result["ok"] is True
    assert result["data"]["matches"] == []
    assert {entry["category"] for entry in result["data"]["categories"]} == set(GUIDE_INDEX)


@pytest.mark.parametrize(
    "payload",
    [
        {"category": "no_such_guide"},
        {"category": "getting_started", "query": "both given"},
        {"category": 7},
    ],
)
def test_invalid_guide_requests_return_structured_errors(all_features: None, payload: dict) -> None:
    result = ue_execute("workflow_guide_get", payload)
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_operation"


def test_guide_operation_is_discoverable(all_features: None) -> None:
    index = ue_capability_get(group="core")
    names = {row[0] for row in index["data"]["operations"]}
    assert "workflow_guide_get" in names
    assert "batch_execute" in names

    context = ue_context_get()
    assert context["data"]["workflow_guides"]["operation"] == "workflow_guide_get"


def test_guide_schema_is_derived_from_wrapper(all_features: None) -> None:
    schema = ue_capability_get(operation="workflow_guide_get", detail="schema")
    properties = schema["data"]["payload_schema"]["properties"]
    assert set(properties) == {"category", "query"}
    assert "required" not in schema["data"]["payload_schema"]
