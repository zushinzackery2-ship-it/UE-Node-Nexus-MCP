"""Issue 4 #2: a catalog spelling names one record, and a shared one fails only its line.

``Actor`` is the real name of /Script/Engine.Actor and only the stripped alias of
/Script/Niagara.NiagaraActor. Resolving both with one weight made the untouched
``CallParentFunction(Actor.UserConstructionScript)`` of an unrelated Blueprint fail a
push. Blueprint-generated classes do share names (one BP_Light_C per folder), so the
mirror writes such a class in full, and lint reports a shared spelling on its own line
instead of ending the check of the whole asset.
"""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.raw.codec import document_from_raw
from ue_node_nexus_mcp.transcode.diff.service import build_plan
from ue_node_nexus_mcp.transcode.diff.common import same_class
from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.lint.service import lint_document
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish, reference
from ue_node_nexus_mcp.transcode.schema.functions import CALLABLE_INDEX, EVENT_INDEX
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock
from ue_node_nexus_mcp.transcode.errors import SyncError
from tests.transcode.fixtures import blueprint_raw, niagara_raw

ACTOR = "/Script/Engine.Actor"
NIAGARA_ACTOR = "/Script/Niagara.NiagaraActor"
NIAGARA_COMPONENT = "/Script/Niagara.NiagaraComponent"
LIGHT = "/Game/Props/A/BP_Light.BP_Light_C"
OTHER_LIGHT = "/Game/Props/B/BP_Light.BP_Light_C"
MESH = "/Script/Engine.StaticMeshComponent"
SCENE = "/Script/Engine.SceneComponent"
DATA = "/Game/Data/A/DA_Tune.DA_Tune_C"
OTHER_DATA = "/Game/Data/B/DA_Tune.DA_Tune_C"
K2 = "/Script/BlueprintGraph.K2Node_"
CONSTRUCTION = ACTOR + ".UserConstructionScript"
PRINT = "/Script/Engine.KismetSystemLibrary.PrintString"
TEXT = """nexus: 1
asset: /Game/Blueprints/BP_Lamp
class: Blueprint
schema: key

[asset]
ParentClass = /Script/Engine.Actor

[components]
Root : SceneComponent
Bulb : {bulb}(parent=Root)

[function UserConstructionScript()]
parent : CallParentFunction(Actor.UserConstructionScript) @ 0,0
print : CallFunction(KismetSystemLibrary.PrintString) @ 300,0
{extra}
entry.then -> parent.execute
parent.then -> print.execute
"""


def classes(*paths: str) -> dict:
    return dict((path, dict(path=path, props=dict())) for path in paths)


def function(path: str, **flags) -> dict:
    return dict(path=path, params=[], node_class="K2Node_CallFunction", **flags)


def catalog(tmp_path, coverage=EVENT_INDEX) -> SchemaLock:
    """The layout the plugin publishes: class records keyed by path, functions by owner path."""
    directory = tmp_path / ".nexus" / "schema" / "key"
    tables = dict(
        asset=dict(classes(ACTOR, NIAGARA_ACTOR, DATA, OTHER_DATA),
                   **{"/Script/Engine.Blueprint": dict(path="/Script/Engine.Blueprint", props=dict(ParentClass=dict(type="class")))}),
        component=classes(NIAGARA_COMPONENT, LIGHT, OTHER_LIGHT, MESH, SCENE),
        k2node=classes(*(K2 + name for name in ("CallParentFunction", "CallFunction", "Event", "FunctionEntry"))),
        niagara_renderer=classes("/Script/Niagara.NiagaraSpriteRendererProperties"),
        callable_functions={CONSTRUCTION: function(CONSTRUCTION, callable=False, event=True),
                            PRINT: function(PRINT, callable=True, event=False)},
    )
    publish(directory, "key", tables, coverage=dict(functions=coverage))
    return SchemaLock(directory, "key")


def component_lines(text: str) -> dict[str, str]:
    """``name : declaration`` rows of the text, keyed by name without the alignment padding."""
    return dict((name.strip(), rest) for name, rest in (line.split(" : ", 1) for line in text.splitlines() if " : " in line))


def diagnostics(text: str, lock: SchemaLock) -> list[tuple[str, int | None]]:
    document, sink = parse(text)
    assert sink.items == [], sink.items
    return [(item.code, item.line) for item in lint_document(document, "blueprint", lock, "BP_Lamp.bp.nexus").items]


def test_a_real_class_name_outranks_the_stripped_alias_of_another_class(tmp_path):
    lock = catalog(tmp_path)

    assert lock.resolve_class("asset", "Actor").path == ACTOR
    assert lock.resolve_class("asset", "NiagaraActor").path == NIAGARA_ACTOR
    # A stripped alias still answers while it is the strongest match there is.
    assert lock.resolve_class("component", "Component").path == NIAGARA_COMPONENT
    with pytest.raises(SyncError) as shared:
        lock.resolve_class("component", "BP_Light_C")
    assert shared.value.code == "schema_ambiguous"
    assert shared.value.details["tier"] == "name"
    assert sorted(shared.value.details["candidates"]) == sorted([LIGHT, OTHER_LIGHT])


def test_a_function_owner_answers_only_to_its_path_or_real_name(tmp_path):
    lock = catalog(tmp_path)

    assert lock.function("Actor", "UserConstructionScript")["path"] == CONSTRUCTION
    assert lock.function(ACTOR, "UserConstructionScript")["path"] == CONSTRUCTION
    # ``Component`` is only NiagaraComponent's stripped alias: an owner never means it.
    assert lock.resolve_class("component", "Component", aliases=False) is None


def test_an_unmodified_construction_script_lints_clean(tmp_path):
    lock = catalog(tmp_path)

    assert diagnostics(TEXT.format(bulb="StaticMeshComponent", extra=""), lock) == []


def test_parent_calls_and_events_are_checked_against_the_event_index_only_when_it_exists(tmp_path):
    missing = TEXT.format(bulb="StaticMeshComponent", extra="").replace("Actor.UserConstructionScript", "Actor.ReceiveTick")
    assert diagnostics(missing, catalog(tmp_path / "events")) == [("unknown_event", 14)]
    # A catalog from before the event index lists callables only; it cannot say.
    assert diagnostics(missing, catalog(tmp_path / "callables", CALLABLE_INDEX)) == []
    # A call still needs a Blueprint-callable function, whichever index the catalog has.
    event_call = TEXT.format(bulb="StaticMeshComponent", extra="").replace("KismetSystemLibrary.PrintString", "Actor.UserConstructionScript")
    assert diagnostics(event_call, catalog(tmp_path / "calls", CALLABLE_INDEX)) == [("unknown_function", 15)]


def test_a_shared_component_class_is_written_in_full_and_a_unique_one_stays_short(tmp_path):
    lock = catalog(tmp_path)
    raw = blueprint_raw()
    components = raw["blueprint"]["components"]
    components.append(dict(components[1], name="Bulb", guid="C-3", props=[], **{"class": LIGHT, "class_short": "BP_Light_C"}))
    components.append(dict(components[1], name="Spark", guid="C-4", props=[], **{"class": NIAGARA_COMPONENT, "class_short": "NiagaraComponent"}))
    components.append(dict(components[1], name="Fresh", guid="C-5", props=[],
                           **{"class": "/Game/Props/New/BP_Fresh.BP_Fresh_C", "class_short": "BP_Fresh_C"}))

    lines = component_lines(emit(document_from_raw(raw, schema=lock)[0]))

    assert lines["Bulb"].startswith(LIGHT + "(parent=Root)"), lines["Bulb"]
    assert lines["Mesh"].startswith("StaticMeshComponent"), lines["Mesh"]
    assert lines["Spark"].startswith("NiagaraComponent"), lines["Spark"]
    # A class newer than the catalog keeps its name: nothing else can answer to it yet.
    assert lines["Fresh"].startswith("BP_Fresh_C"), lines["Fresh"]
    # Without a catalog every class keeps the name the editor reported.
    assert component_lines(emit(document_from_raw(raw)[0]))["Bulb"].startswith("BP_Light_C(parent=Root)")


def test_an_asset_of_a_shared_generated_class_names_it_in_full(tmp_path):
    lock = catalog(tmp_path)
    raw = dict(raw_version=1, asset_path="/Game/Data/DA_Rain.DA_Rain", kind="asset", class_short="DA_Tune_C",
               schema_key="key", props=[])
    raw["class"] = DATA
    assert document_from_raw(raw, schema=lock)[0].header.cls == DATA
    raw["class"], raw["class_short"] = "/Script/Engine.Blueprint", "Blueprint"
    assert document_from_raw(raw, schema=lock)[0].header.cls == "Blueprint"
    assert reference(None, "asset", DATA, "DA_Tune_C") == "DA_Tune_C"


def test_a_shared_spelling_fails_its_own_line_and_the_rest_is_still_checked(tmp_path):
    lock = catalog(tmp_path)
    text = TEXT.format(bulb="BP_Light_C", extra="lost : CallFunction(Nowhere.Nothing) @ 600,0")

    found = diagnostics(text, lock)

    assert ("ambiguous_class", 11) in found, found
    assert ("unknown_function", 16) in found, found
    document, _ = parse(text)
    message = next(item.message for item in lint_document(document, "blueprint", lock).items if item.code == "ambiguous_class")
    assert LIGHT in message and OTHER_LIGHT in message
    assert diagnostics(TEXT.format(bulb=LIGHT, extra=""), lock) == []


def test_a_shared_asset_class_fails_the_class_line(tmp_path):
    lock = catalog(tmp_path)
    document, _ = parse("nexus: 1\nasset: /Game/Data/DA_Rain\nclass: DA_Tune_C\nschema: key\n\n[asset]\nScale = 2\n")

    found = [(item.code, item.line) for item in lint_document(document, "asset", lock).items]

    assert found == [("ambiguous_class", 3)]


def test_a_shared_renderer_spelling_is_reported_on_the_renderer_line(tmp_path):
    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", dict(niagara_renderer=classes("/Script/Niagara.NiagaraSpriteRendererProperties",
                                                            "/Script/FluidFX.NiagaraSpriteRendererProperties")))
    lock = SchemaLock(directory, "key")
    document, _ = parse(emit(document_from_raw(niagara_raw())[0]))
    renderer = next(decl for section in document.find_sections("renderers") for decl in section.decls())

    found = [(item.code, item.line) for item in lint_document(document, "niagara_system", lock).items]

    assert ("ambiguous_class", renderer.line) in found, found


def test_an_added_component_carries_the_class_the_catalog_resolved(tmp_path):
    lock = catalog(tmp_path)
    base, _ = parse(TEXT.format(bulb="StaticMeshComponent", extra=""))
    local, _ = parse(TEXT.format(bulb="StaticMeshComponent", extra="").replace(
        "Bulb : StaticMeshComponent(parent=Root)", "Bulb : StaticMeshComponent(parent=Root)\nGlow : NiagaraComponent(parent=Root)"))

    plan = build_plan(local, base, "blueprint", dict(), lock)

    added = next(verb for verb in plan.verbs if verb.op == "bp_component_add")
    assert added.args["class"] == NIAGARA_COMPONENT


def test_a_mirror_written_before_the_full_path_is_the_same_class(tmp_path):
    lock = catalog(tmp_path)

    assert same_class(lock, "component", "BP_Light_C", LIGHT)
    assert not same_class(lock, "component", OTHER_LIGHT, LIGHT)
