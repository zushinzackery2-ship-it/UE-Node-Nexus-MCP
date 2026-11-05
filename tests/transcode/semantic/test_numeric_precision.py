"""Representable edits survive comparison; native float32 spellings still agree."""

import pytest

from ue_node_nexus_mcp.transcode.text.semantic import equivalent, value
from ue_node_nexus_mcp.transcode.text.writes import write_differences
from ue_node_nexus_mcp.transcode.collaboration.merge.engine import Comparison


@pytest.mark.parametrize("kind,before,after", [
    ("float", "0", "0.0000005"),
    ("double", "1000000000", "1000000000.5"),
    ("float", "1.777778", "1.77777779"),
])
def test_representable_numeric_edits_are_not_discarded(kind, before, after):
    assert not equivalent(value(before, kind), value(after, kind))
    assert write_differences(after, before, kind)
    comparison = Comparison("/Game/Value.Value")
    assert comparison.merge(value(before, kind), value(before, kind), value(after, kind)) == value(after, kind)
    assert comparison.conflicts == []


def test_float32_export_rounding_is_already_canonical():
    assert equivalent(value("0.7", "float"), value("0.699999988", "float"))
