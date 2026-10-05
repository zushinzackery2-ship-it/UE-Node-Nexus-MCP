"""Completion tests bind real PNG bytes to one request, including interruption."""

import hashlib
import json
from pathlib import Path
import struct
import zlib

import pytest

from ue_node_nexus_mcp.safety.capture.png import inspect_png
from ue_node_nexus_mcp.safety.capture.status import status

IDENTIFIER = "0123456789abcdef0123456789abcdef"


def png_bytes() -> bytes:
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0\xff")) + chunk(b"IEND", b""))


def receipt(tmp_path: Path, state="completed"):
    path = tmp_path / ("shot_" + IDENTIFIER + ".png")
    image = png_bytes()
    path.write_bytes(image)
    record = dict(capture_protocol=1, capture_id=IDENTIFIER, file_path=str(path), state=state,
                  width=1, height=1, size_bytes=len(image), sha1=hashlib.sha1(image).hexdigest(),
                  producer=dict(pid=1, process_created="unused", executable="unused"))
    journal = Path(str(path) + ".capture.json")
    journal.write_text(json.dumps(record), encoding="utf-8")
    return path, journal, record


def test_verified_request_completion(tmp_path):
    path, _, _ = receipt(tmp_path)
    result = status(str(path), IDENTIFIER)
    assert result["ok"] and result["data"]["done"] and result["data"]["state"] == "completed"
    assert result["data"]["width"] == result["data"]["height"] == 1


def test_unreal_uppercase_guid_is_a_valid_identity(tmp_path):
    path, journal, record = receipt(tmp_path)
    record["capture_id"] = IDENTIFIER.upper()
    journal.write_text(json.dumps(record), encoding="utf-8")
    assert status(str(path), IDENTIFIER.upper())["ok"]


def test_old_file_without_receipt_cannot_complete_a_request(tmp_path):
    path = tmp_path / "old.png"
    path.write_bytes(png_bytes())
    result = status(str(path))
    assert result["data"]["exists"] and result["data"]["state"] == "untracked"
    assert not result["data"]["done"]


@pytest.mark.parametrize("field,value", [("width", 2), ("sha1", "wrong"), ("capture_id", "f" * 32)])
def test_mismatched_completion_refused(tmp_path, field, value):
    path, journal, record = receipt(tmp_path)
    record[field] = value
    journal.write_text(json.dumps(record), encoding="utf-8")
    result = status(str(path), IDENTIFIER)
    assert not result["ok"] and result["error"]["code"] == "capture_artifact_invalid"


@pytest.mark.parametrize("state", ["queued", "rendering"])
def test_dead_producer_does_not_report_pending_forever(tmp_path, monkeypatch, state):
    path, _, _ = receipt(tmp_path, state)
    monkeypatch.setattr("ue_node_nexus_mcp.safety.capture.status.is_alive", lambda identity: False)
    result = status(str(path), IDENTIFIER)
    assert not result["ok"] and result["data"]["state"] == "interrupted" and result["data"]["done"]


def test_failure_remains_failure_even_when_image_exists(tmp_path):
    path, journal, record = receipt(tmp_path, "failed")
    record["error_code"] = "capture_request_replaced"
    journal.write_text(json.dumps(record), encoding="utf-8")
    result = status(str(path), IDENTIFIER)
    assert not result["ok"] and result["error"]["code"] == "capture_request_replaced"


@pytest.mark.parametrize("mutation", [lambda data: data[:-5], lambda data: data[:20] + b"x" + data[21:], lambda data: b"fake"])
def test_corrupt_png_is_refused(tmp_path, mutation):
    path = tmp_path / "bad.png"
    path.write_bytes(mutation(png_bytes()))
    with pytest.raises(ValueError):
        inspect_png(path)


def test_private_bridge_reuses_admission_retry_without_rebinding(use_bridge, monkeypatch):
    from ue_node_nexus_mcp import runtime
    class Private:
        def __init__(self):
            self.calls = 0

        def call(self, operation, payload):
            self.calls += 1
            if self.calls == 1:
                return dict(ok=False, error=dict(code="bridge_busy", message="engine frame owns streaming"))
            return dict(ok=True, data=dict(owner="private"))

    class Main:
        def call(self, operation, payload):
            raise AssertionError("the user's bridge must not be touched")

    use_bridge(Main())
    monkeypatch.setattr(runtime.time, "sleep", lambda delay: None)
    private = Private()
    result = runtime.call_bridge("project_context_get", dict(), client=private)
    assert result["ok"] and result["data"]["owner"] == "private" and private.calls == 2
