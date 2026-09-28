"""Shared fixtures used across the regression suites."""

from types import SimpleNamespace
from ue_node_nexus_mcp.transcode.collaboration.history import History
from ue_node_nexus_mcp.transcode.collaboration.store import Store


def instance_text(amount="0.338"):
    return ("nexus: 1\nasset: /Game/MI_Test\nclass: MaterialInstanceConstant\nschema: key\n"
            f"[scalar]\nAmount = {amount}\n")


def instance_raw(amount="0.337999999"):
    return dict(raw_version=1, asset_path="/Game/MI_Test.MI_Test", kind="material_instance", schema_key="key",
                class_short="MaterialInstanceConstant", props=[], instance=dict(scalar=[dict(name="Amount", type="float", value=amount)]))


def receipt_workspace(tmp_path, candidate):
    store = Store(tmp_path)
    history = History(store)
    asset = candidate["semantic"]["header"]["asset"]
    commit = history.create(history.tree(dict([(asset, store.objects.put("snapshot", candidate))])), [], "candidate")
    return SimpleNamespace(store=store, history=history, schema=None, state=dict(files=dict()), root=tmp_path), commit, asset


def self_graph_raw():
    source = dict(guid="SELF", class_short="Self", supported=True, x=0, y=0, config=dict(), props=[],
                  pins=[dict(guid="SP", name="self", dir="out", type=dict(category="object"), linked=["CP"])])
    call = dict(guid="CALL", class_short="CallFunction", supported=True, x=0, y=0,
                config=dict(function_owner="/Script/Engine.Actor", function_name="K2_DestroyActor"), props=[],
                pins=[dict(guid="CP", name="self", dir="in", type=dict(category="object"), linked=["SP"])])
    return dict(raw_version=1, asset_path="/Game/BP_Test.BP_Test", kind="blueprint", schema_key="key",
                class_short="Blueprint", props=[], blueprint=dict(graphs=[dict(name="EventGraph", nodes=[source, call])]))
