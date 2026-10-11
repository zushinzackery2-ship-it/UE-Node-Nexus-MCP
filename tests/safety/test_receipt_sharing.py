"""A screenshot producer may replace its receipt while status still reads the previous version."""

import sys

import pytest

from ue_node_nexus_mcp.safety.capture.receipt import open_receipt


@pytest.mark.skipif(sys.platform != "win32", reason="Windows requires explicit delete sharing")
def test_reader_allows_producer_to_replace_receipt(tmp_path):
    path = tmp_path / "request.png.capture.json"
    path.write_text("rendering", encoding="utf-8")
    with open_receipt(path) as reader:
        path.unlink()
        path.write_text("completed", encoding="utf-8")
        assert reader.read() == "rendering"
    assert path.read_text(encoding="utf-8") == "completed"


def test_receipt_reader_accepts_utf8_bom_and_releases_handle(tmp_path):
    path = tmp_path / "request.png.capture.json"
    path.write_text("completed", encoding="utf-8-sig")
    with open_receipt(path) as reader:
        assert reader.read() == "completed"
    path.unlink()
    assert not path.exists()
