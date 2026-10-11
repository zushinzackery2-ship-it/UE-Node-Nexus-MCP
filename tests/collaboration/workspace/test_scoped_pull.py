"""A selected pull replays all local layers without consuming other assets."""

from copy import deepcopy

import pytest

from ..helpers import ASSET, commit, repository
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.history.integrate import integrate
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout

OTHER = "/Game/Test/Other.Other"


@pytest.mark.parametrize("layer", ["unstaged", "staged", "mixed", "committed", "deleted", "staged_deleted"])
def test_selected_pull_preserves_outside_bytes_and_history_layers(tmp_path, layer):
    store, root = repository(tmp_path)
    history = History(store)
    entries = history.entries(root)
    other = deepcopy(store.objects.data(entries[ASSET], "snapshot"))
    other["semantic"]["header"]["asset"] = "/Game/Test/Other"
    from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, text_of

    entries[OTHER] = store.snapshot(capture(text_of(other), None, "other", "asset"))
    base = history.create(history.tree(entries), [root], "two assets")
    source, target = checkout(store, base), checkout(store, base)
    incoming = commit(source, "A", "10")
    path = target.root / target.state["files"][OTHER]
    path.write_bytes(path.read_bytes().replace(b"A = 0", b"A = 20"))
    if layer in ("staged", "mixed", "committed", "staged_deleted"):
        target.stage(paths=[OTHER])
    if layer == "committed":
        target.commit("outside scope")
    if layer == "mixed":
        path.write_bytes(path.read_bytes().replace(b"A = 20", b"A = 30"))
    if layer in ("deleted", "staged_deleted"):
        path.unlink()
    if layer == "staged_deleted":
        target.stage(paths=[OTHER], delete=True)
    if path.exists():
        path.write_bytes(b"\xef\xbb\xbf# Preserve outside bytes\r\n" + path.read_bytes().replace(b"\n", b"\r\n"))
    before_bytes = path.read_bytes() if path.exists() else None
    before_index = history.entries(target.state["index"]).get(OTHER)
    before_head = history.entries(target.state["head"])[OTHER]
    before_relative = target.state["files"][OTHER]
    result = integrate(target, "pull", incoming, paths=[ASSET])
    assert result["status"] == "completed"
    assert (path.read_bytes() if path.exists() else None) == before_bytes
    assert history.entries(target.state["index"]).get(OTHER) == before_index
    assert history.entries(target.state["head"])[OTHER] == before_head
    assert target.state["files"][OTHER] == before_relative
    assert "A = 10" in (target.root / target.state["files"][ASSET]).read_text(encoding="utf-8")
