"""viewport_capture's filename is an extension-free name, declared once and enforced on both sides."""

import re

import pytest

from tests.compile_check.check_sources import CORE
from tests.support.bridges import RecordingBridge
from ue_node_nexus_mcp.facade.capabilities import ue_capability_get
from ue_node_nexus_mcp.facade.entry import ue_execute
from ue_node_nexus_mcp.registry.payloads.schema import payload_schema_for
from ue_node_nexus_mcp.safety.capture import request
from ue_node_nexus_mcp.tools import viewport as tools_viewport

VALID = ("before_fix", "HeadV5-Before", "a" * 100, "0")
INVALID = ("HeadV5_Before.png", "Shots/HeadV5", "C:\\Shots\\HeadV5", "", "a" * 101, "截图", "name\n", "two words")


def test_schema_declares_the_name_contract():
    fields = payload_schema_for("viewport_capture")["properties"]
    filename = fields["filename"]
    assert (filename["pattern"], filename["minLength"], filename["maxLength"]) == (request.FILENAME_PATTERN, 1, 100)
    assert "without directory or extension" in filename["description"]
    assert "<filename>_<capture_id>.png" in filename["description"]
    assert (fields["timeout_seconds"]["minimum"], fields["timeout_seconds"]["maximum"]) == (1, 120)
    assert fields["target"]["enum"] == ["level", "active", "pie"] and fields["target"]["description"]


def test_capability_schema_serves_the_constraints(all_features):
    result = ue_capability_get(operation="viewport_capture", detail="schema")
    assert result["data"]["payload_schema"]["properties"]["filename"]["pattern"] == request.FILENAME_PATTERN


@pytest.mark.parametrize("name", VALID + INVALID)
def test_pattern_and_wrapper_agree(monkeypatch, name):
    calls = []
    monkeypatch.setattr(tools_viewport, "_call", lambda operation, payload: calls.append(payload) or dict(ok=True))
    accepted = re.fullmatch(request.FILENAME_PATTERN, name) is not None
    assert accepted == (name in VALID)
    if accepted:
        tools_viewport.viewport_capture(filename=name, dry_run=True)
        assert calls[0]["filename"] == name
        return
    with pytest.raises(ValueError, match="filename must be 1..100 letters"):
        tools_viewport.viewport_capture(filename=name)
    assert calls == []


@pytest.mark.parametrize(("arguments", "field"), [
    (dict(filename=7), "filename"), (dict(target="game"), "target"), (dict(timeout_seconds=0), "timeout_seconds"),
    (dict(timeout_seconds=121), "timeout_seconds"), (dict(timeout_seconds=True), "timeout_seconds"),
    (dict(show_ui="yes"), "show_ui"), (dict(dry_run=1), "dry_run")])
def test_wrapper_names_the_violated_field(monkeypatch, arguments, field):
    monkeypatch.setattr(tools_viewport, "_call", lambda *_args: pytest.fail("invalid request reached the editor"))
    with pytest.raises(ValueError, match=f"^{field} must"):
        tools_viewport.viewport_capture(**arguments)


def test_omitted_name_uses_the_editor_default(monkeypatch):
    calls = []
    monkeypatch.setattr(tools_viewport, "_call", lambda operation, payload: calls.append(payload) or dict(ok=True))
    tools_viewport.viewport_capture(dry_run=True)
    assert calls[0]["filename"] is None


def test_native_handler_enforces_the_declared_contract():
    source = (CORE / "Private/Viewport/Capture/NexusCaptureRequest.cpp").read_text(encoding="utf-8")
    header = (CORE / "Private/Viewport/Capture/NexusCapture.h").read_text(encoding="utf-8")
    assert f'TEXT("{request.FILENAME_PATTERN}")' in source
    assert f"MaxScreenshotNameLength = {request.FILENAME_MAX_LENGTH};" in header
    assert f"MinCaptureTimeout = {request.TIMEOUT_MINIMUM};" in header
    assert f"MaxCaptureTimeout = {request.TIMEOUT_MAXIMUM};" in header
    assert f'Name = TEXT("{request.DEFAULT_FILENAME}")' in header
    for rule in (request.FILENAME_RULE, request.TARGET_RULE, request.TIMEOUT_RULE):
        assert f'TEXT("{rule}")' in source
    assert ", ".join(f'TEXT("{target}")' for target in request.TARGETS) in source
    assert "Target.Equals(Candidate, ESearchCase::CaseSensitive)" in source
    assert "FChar::IsAlnum" not in source


def test_native_field_errors_reach_the_caller_unchanged(all_features, use_bridge):
    details = dict(field="filename", reason="extension", value="HeadV5_Before.png", pattern=request.FILENAME_PATTERN)
    use_bridge(RecordingBridge(dict(ok=False, error=dict(code="invalid_request", message=request.FILENAME_RULE,
                                                         details=details), diagnostics=[], warnings=[])))
    result = ue_execute("viewport_capture", dict(filename="HeadV5_Before.png"))
    assert result["ok"] is False
    assert result["error"]["code"] == "invalid_request"
    assert result["error"]["details"]["field"] == "filename"
