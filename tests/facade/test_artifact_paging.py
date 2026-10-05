"""Issue 19: an artifact too large for one frame must still be readable.

The artifact mechanism exists because a response did not fit. Serving it whole
made it fail in exactly that case, so paging is the point, not an option.
"""

from __future__ import annotations

import json

import pytest

from ue_node_nexus_mcp import facade_artifact
from ue_node_nexus_mcp.facade_state import facade_state
from ue_node_nexus_mcp.server import ue_read


@pytest.fixture
def small(monkeypatch):
    """A tiny budget makes the paging path reachable without a huge payload."""
    monkeypatch.setattr(facade_artifact, "ARTIFACT_PAGE_BYTE_BUDGET", 512)
    return 512


def stored(payload):
    return facade_state.store_artifact("conflicts", payload)


def page(artifact_id, **query):
    return ue_read("artifact", query=dict(artifact_id=artifact_id, **query))


def test_a_payload_that_fits_is_returned_whole(small):
    artifact = stored({"rows": ["a", "b"]})
    response = page(artifact.artifact_id)
    assert response["ok"] is True
    assert response["data"]["rows"] == ["a", "b"]
    assert response["data"]["runtime_observed"] is False
    assert response["data"]["runtime_diagnostics"]["status"] == "unknown"


def test_an_oversized_payload_is_paged_and_reassembles_exactly(small):
    payload = {"conflicts": [{"id": index, "reason": "duplicate alias"} for index in range(200)]}
    artifact = stored(payload)

    first = page(artifact.artifact_id)["data"]
    assert first["encoding"] == "json-text"
    assert first["truncated"] is True and first["cursor"] == 0
    assert first["chunk_bytes"] <= small
    assert first["next_read"]["args"]["query"]["cursor"] == first["next_cursor"]

    text, cursor, pages = "", 0, 0
    while cursor is not None:
        chunk = page(artifact.artifact_id, cursor=cursor)["data"]
        assert chunk["total_bytes"] == first["total_bytes"]
        text += chunk["chunk"]
        cursor = chunk["next_cursor"]
        pages += 1
    assert pages > 1
    assert len(text) == first["total_bytes"]
    assert json.loads(text) == payload


def test_a_caller_may_ask_for_less_but_never_for_more_than_the_frame_budget(small):
    artifact = stored({"conflicts": [{"id": index} for index in range(200)]})
    assert page(artifact.artifact_id, limit_bytes=64)["data"]["chunk_bytes"] <= 64
    assert page(artifact.artifact_id, limit_bytes=10 ** 9)["data"]["chunk_bytes"] <= small


@pytest.mark.parametrize("query,message", [
    ({"cursor": -1}, "cursor"),
    ({"cursor": "0"}, "cursor"),
    ({"limit_bytes": 0}, "limit_bytes"),
    ({"limit_bytes": True}, "limit_bytes"),
])
def test_a_page_request_that_cannot_be_honored_is_refused(small, query, message):
    artifact = stored({"conflicts": [{"id": index} for index in range(200)]})
    response = page(artifact.artifact_id, **query)
    assert response["ok"] is False
    assert response["error"]["code"] == "invalid_request"
    assert message in response["error"]["message"]


def test_a_cursor_past_the_end_says_how_long_the_artifact_is(small):
    artifact = stored({"conflicts": [{"id": index} for index in range(200)]})
    total = page(artifact.artifact_id)["data"]["total_bytes"]
    response = page(artifact.artifact_id, cursor=total + 1)
    assert response["ok"] is False
    assert response["error"]["details"]["total_bytes"] == total


def test_a_list_under_a_key_with_dots_is_paged_through_the_call_the_page_names(small):
    """Issue 4 #3: asset paths contain dots, so the named follow-up call carries the keys as a list."""
    asset = "/Game/Materials/M_Glass.M_Glass"
    payload = {"conflicts": {asset: [{"id": index, "field": "graph.links"} for index in range(120)]}}
    artifact = stored(payload)

    first = page(artifact.artifact_id)["data"]
    call = first["page_lists_with"]
    assert first["lists"] == {"conflicts." + asset: 120}
    assert call["args"]["query"]["path"] == ["conflicts", asset]

    items, query = [], call["args"]["query"]
    while query is not None:
        response = ue_read(call["args"]["target"], query=query)
        assert response["ok"] is True, response
        items.extend(response["data"]["items"])
        assert response["data"]["path"] == ["conflicts", asset]
        following = response["data"]["next_read"]
        query = following["args"]["query"] if following else None
    assert items == payload["conflicts"][asset]


def test_a_dotted_path_still_reads_keys_without_dots():
    assert facade_artifact.path_argument(["data", "conflicts"]) == "data.conflicts"
    value, walked, problem = facade_artifact.locate({"data": {"a.b": [1, 2]}}, ["data", "a.b"])
    assert value == [1, 2] and walked == ["data", "a.b"] and problem is None
    _, _, missing = facade_artifact.locate({"data": {"a.b": [1, 2]}}, "data.a.b")
    assert missing["available"] == ["a.b"]


def test_non_ascii_content_is_escaped_so_a_byte_cursor_never_splits_a_character(small):
    payload = {"conflicts": [{"note": "中文说明 " + str(index)} for index in range(200)]}
    artifact = stored(payload)
    text, cursor = "", 0
    while cursor is not None:
        chunk = page(artifact.artifact_id, cursor=cursor)["data"]
        text += chunk["chunk"]
        cursor = chunk["next_cursor"]
    assert text.isascii()
    assert json.loads(text) == payload
