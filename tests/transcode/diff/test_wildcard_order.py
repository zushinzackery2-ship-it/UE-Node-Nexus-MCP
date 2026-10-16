"""Prevent native wildcard arrays from adopting the first consumer's base class."""

from ue_node_nexus_mcp.transcode.diff.graph import _diff_connects, _link_key
from ue_node_nexus_mcp.transcode.diff.plan import AssetPlan
from ue_node_nexus_mcp.transcode.text.model import Link, Section


def connect_plan(kind, links, base=None):
    local = Section("graph", entries=links)
    declarations = dict((link.src, None) for link in links if link.src != "entry")
    plan = AssetPlan("/Game/Test/Wildcard", kind)
    known = dict()
    local_links = {_link_key(link, known, kind, dict()): link for link in local.links()}
    base_links = {_link_key(link, known, kind, dict()): link for link in (base.links() if base else [])}
    _diff_connects(local_links, base_links, declarations, dict(entry=None), plan, kind, dict(), set())
    return [verb.to_payload() for verb in plan.verbs]


def test_native_array_type_precedes_base_class_consumers():
    links = [Link("loop", "Array Element", "valid", "InputObject"),
        Link("loop", "Array Element", "copy", "Source"),
        Link("entry", "Materials", "loop", "Array"),
        Link("loop", "Array Element", "parameter", "self")]
    operations = connect_plan("blueprint", links)
    assert operations[0]["from"] == "entry"
    assert operations[0]["to_pin"] == "Array"
    assert len(operations) == 4


def test_array_get_type_precedes_its_object_consumer():
    links = [Link("get", "Item", "valid", "InputObject"),
        Link("entry", "Materials", "get", "TargetArray")]
    assert connect_plan("blueprint", links)[0]["to_pin"] == "TargetArray"


def test_material_connection_order_is_preserved():
    links = [Link("a", None, "b", "Input"), Link("c", None, "d", "Array")]
    assert [operation["from"] for operation in connect_plan("material", links)] == ["a", "c"]
