from __future__ import annotations

from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import from_raw
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.collaboration.workspace import checkout

ASSET = "/Game/Test/Data.Data"


def repository(tmp_path):
    store = Store(tmp_path)
    history = History(store)
    raw = dict(raw_version=1, asset_path=ASSET, class_short="DataAsset", kind="asset", schema_key="test-schema",
               props=[dict(name=name, type="float", value="0", default="99") for name in ("A", "B")])
    raw["class"] = "/Script/Engine.DataAsset"
    snapshot = store.objects.put("snapshot", from_raw(raw))
    root = history.create(history.tree(dict([(ASSET, snapshot)])), [], "import")
    store.move("refs/heads/main", root, None)
    return store, root


def edit(workspace, field: str, value: str):
    file = workspace.root / workspace.state["files"][ASSET]
    lines = file.read_text(encoding="utf-8").splitlines()
    lines = [f"{field} = {value}" if line.startswith(field + " =") else line for line in lines]
    file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def commit(workspace, field: str, value: str):
    edit(workspace, field, value)
    return workspace.commit(f"{field}={value}", all_files=True)["commit_id"]


def values(workspace):
    from ue_node_nexus_mcp.transcode.parser import parse

    file = workspace.root / workspace.state["files"][ASSET]
    document, _ = parse(file.read_text(encoding="utf-8"))
    return dict((prop.key, prop.value) for prop in document.section("asset").props())
