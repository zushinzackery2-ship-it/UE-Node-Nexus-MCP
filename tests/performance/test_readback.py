"""Receipt validation must stay linear in the submitted graph size."""

from types import SimpleNamespace

from ue_node_nexus_mcp.transcode.collaboration.apply import readback
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture


def test_large_graph_receipt_indexes_entities_once(tmp_path, monkeypatch):
    count = 1200
    text = "nexus: 1\nasset: /Game/M_Test\nclass: Material\n[graph]\n"
    text += "".join(f"n{index} : Constant(R={index}) @ {index},0\n" for index in range(count))
    snapshot = capture(text, None, "author", "material")
    visited = [0]
    original = readback.entities

    class Entries(dict):
        def __iter__(self):
            for key in super().__iter__():
                visited[0] += 1
                yield key

        def items(self):
            for key in self:
                yield key, self[key]

    monkeypatch.setattr(readback, "entities", lambda value: Entries(original(value)))
    workspace = SimpleNamespace(schema=None, state=dict(files=dict()), root=tmp_path)
    record = dict(id="scale", asset="/Game/M_Test.M_Test",
                  request=dict(plan=[dict(op="create_node", id=f"n{index}") for index in range(count)]))
    readback.verify_result(workspace, record, snapshot, snapshot)
    assert visited[0] <= count * 4, f"receipt matching traversed {visited[0]} entries for {count} nodes"
