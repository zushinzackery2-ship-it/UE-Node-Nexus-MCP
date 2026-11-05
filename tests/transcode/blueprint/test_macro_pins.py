"""A macro instance's links must use the tunnel pins its macro graph declares.

``ForLoop`` names its entry ``execute`` while ``ForEachLoop`` names the same pin
``Exec``; only the macro graph states which. The catalog records those tunnel
pins, so lint can refuse a guessed spelling before the editor does.
"""

import pytest

from ue_node_nexus_mcp.transcode.blueprint.pins import node_pins
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.text.parser import parse

FOR_LOOP = "/Engine/StandardMacros.StandardMacros.ForLoop"
FOR_EACH = "/Engine/StandardMacros.StandardMacros.ForEachLoop"


def macro(path, name, inputs, outputs):
    pins = ([dict(name=item, dir="in", type=dict(category="exec" if item in ("execute", "Exec") else "int")) for item in inputs]
            + [dict(name=item, dir="out", type=dict(category="exec" if item == "then" else "int")) for item in outputs])
    return path, dict(path=path, owner=path.rsplit(".", 1)[0], name=name, pins=pins, coverage="reflected")


@pytest.fixture
def schema(tmp_path):
    nodes = dict(("/Script/BlueprintGraph.K2Node_" + name, dict(dynamic_pins=True))
                 for name in ("MacroInstance", "ExecutionSequence", "IfThenElse"))
    macros = dict([macro(FOR_LOOP, "ForLoop", ("execute", "FirstIndex", "LastIndex"), ("LoopBody", "Index", "then")),
                   macro(FOR_EACH, "ForEachLoop", ("Exec", "Array"), ("LoopBody", "Array Element", "Completed"))])
    directory = tmp_path / "schema"
    publish(directory, "key", dict(k2node=nodes, macros=macros))
    return SchemaLock(directory, "key")


def document(body):
    result, sink = parse("nexus: 1\nasset: /Game/BP_Macro\nclass: Blueprint\nschema: key\n" + body)
    assert not sink.has_errors, sink.items
    return result


def errors(doc, schema):
    return [(item.code, item.message) for item in lint_document(doc, "blueprint", schema).errors()]


def test_the_declared_entry_pin_is_accepted(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForLoop)\n"
                   "entry.then -> loop.execute\nloop.then -> result.execute\n")
    assert errors(doc, schema) == []


def test_an_undeclared_entry_pin_names_the_real_one(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForLoop)\n"
                   "entry.then -> loop.Exec\n")
    found = errors(doc, schema)
    assert [code for code, _ in found] == ["unknown_pin"]
    assert "execute" in found[0][1] and "'Exec'" in found[0][1]


def test_a_second_macro_keeps_its_own_spelling(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForEachLoop)\n"
                   "entry.then -> loop.Exec\n")
    assert errors(doc, schema) == []


def test_pin_names_follow_the_apply_phase_case_insensitively(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForLoop)\n"
                   "entry.then -> loop.Execute\n")
    assert errors(doc, schema) == []


def test_an_unknown_macro_is_not_guessed(schema):
    doc = document("[function Step()]\nloop : MacroInstance(ProjectMacros.Custom)\n"
                   "entry.then -> loop.Exec\n")
    assert errors(doc, schema) == []


def test_exported_pins_are_the_mirror_spelling(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForEachLoop, pins=2)\n"
                   "entry.then -> loop.Exec\n")
    section = doc.section("function")
    loop = section.decl_map()["loop"]
    loop.meta["pins"] = [dict(name="Exec", dir="in", type=dict(category="exec")),
                         dict(name="Array", dir="in", type=dict(category="array"))]
    assert [item["name"] for item in node_pins(loop, doc, section, schema)] == ["Exec", "Array"]
    assert errors(doc, schema) == []


def test_exported_pins_reject_a_name_the_export_never_had(schema):
    doc = document("[function Step()]\nloop : MacroInstance(StandardMacros.ForEachLoop)\n"
                   "entry.then -> loop.execute\n")
    section = doc.section("function")
    loop = section.decl_map()["loop"]
    loop.meta["pins"] = [dict(name="Exec", dir="in", type=dict(category="exec"))]
    found = errors(doc, schema)
    assert [code for code, _ in found] == ["unknown_pin"]
    assert "Exec" in found[0][1]


def test_the_catalog_resolves_a_macro_by_its_short_library_name(schema):
    assert schema.macro("StandardMacros.ForLoop")["path"] == FOR_LOOP
    assert schema.macro(FOR_LOOP)["path"] == FOR_LOOP
    assert schema.macro("StandardMacros.Missing") is None


def test_a_shared_macro_spelling_is_not_guessed(tmp_path):
    first, second = "/Game/A/BP_Macros.BP_Macros.Loop", "/Game/B/BP_Macros.BP_Macros.Loop"
    directory = tmp_path / "schema"
    publish(directory, "key", dict(
        k2node=dict(("/Script/BlueprintGraph.K2Node_" + name, dict(dynamic_pins=True)) for name in ("MacroInstance",)),
        macros=dict([macro(first, "Loop", ("execute",), ("then",)), macro(second, "Loop", ("execute",), ("then",))])))
    lock = SchemaLock(directory, "key")
    doc = document("[function Step()]\nloop : MacroInstance(BP_Macros.Loop)\nentry.then -> loop.Exec\n")

    assert errors(doc, lock) == []
