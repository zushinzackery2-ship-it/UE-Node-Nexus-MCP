"""A repeated node identity in a material export is refused instead of merged.

``MaterialExpressionGuid`` is editor-only data that legacy assets can repeat across
expressions (``MF_ParallaxOcclusionMapping`` repeats it 18 times over 58 nodes). The
decoder keys nodes by that value, so a repeat used to merge two nodes and rewire their
links, and the bridge resolved the same key to whichever node it found first.
"""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from .fixtures import material_raw


def test_a_repeated_material_node_identity_is_refused():
    raw = material_raw()
    raw["graph"]["nodes"][1]["guid"] = raw["graph"]["nodes"][0]["guid"]

    with pytest.raises(SyncError) as failure:
        document_from_raw(raw)

    assert failure.value.code == "invalid_raw"
    assert "re-pull this asset" in str(failure.value)


def test_unique_material_node_identities_still_decode():
    raw = material_raw()
    _, ids, _ = document_from_raw(raw)
    assert len(ids) == len(raw["graph"]["nodes"])
