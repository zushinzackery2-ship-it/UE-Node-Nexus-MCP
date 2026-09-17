"""Ambiguous or repeated references must never reach the mirror as a guess.

Engine libraries ship a superseded script beside the current one under the same short
name (``InitializeParticle``, ``SubUVAnimation``), and legacy material data can repeat
an editor GUID across several expressions. Both cases used to resolve to whichever
record or node happened to come first, which silently retargets an edit.
"""

from __future__ import annotations

from ue_node_nexus_mcp.transcode.codec import document_from_raw
from ue_node_nexus_mcp.transcode.diff import build_plan
from ue_node_nexus_mcp.transcode.emitter import emit
from ue_node_nexus_mcp.transcode.lint import lint_document
from ue_node_nexus_mcp.transcode.parser import parse
from ue_node_nexus_mcp.transcode.schema.catalog import publish
from ue_node_nexus_mcp.transcode.schema.lock import SchemaLock

from .fixtures import niagara_raw

CURRENT = "/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"
SUPERSEDED = "/Niagara/Modules/Spawn/Initialization/InitializeParticle.InitializeParticle"
SPAWN_RATE = "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"
GRAVITY = "/Niagara/Modules/Update/Forces/GravityForce.GravityForce"

INPUTS = {CURRENT: ["Lifetime", "Color"], SUPERSEDED: ["Lifetime", "Color"], SPAWN_RATE: ["SpawnRate"], GRAVITY: ["Gravity"]}


def module_catalog(tmp_path, deprecated: tuple[str, ...] = ()) -> SchemaLock:
    """Catalog shaped the way the plugin writes it: record keys are full UE paths."""
    directory = tmp_path / ".nexus" / "schema" / "key"
    publish(directory, "key", dict(niagara_modules={
        path: dict(path=path, short=path.rsplit(".", 1)[-1], deprecated=path in deprecated,
                   inputs=[dict(name=name, type="float") for name in names])
        for path, names in INPUTS.items()
    }))
    return SchemaLock(directory, "key")


def module_lines(document) -> dict[str, str]:
    lines = emit(document).splitlines()
    return dict((line.split(":", 1)[0].strip(), line.split(":", 1)[1].strip()) for line in lines if ":" in line and " : " in line)


def module_diagnostics(document, lock):
    """Diagnostics about module resolution only; renderer classes are another catalog."""
    return [item.code for item in lint_document(document, "niagara_system", lock, "NS_Rain.ns.nexus").items
            if item.code in ("ambiguous_module", "unknown_module", "unknown_input")]


def test_a_short_name_that_two_scripts_answer_to_is_written_in_full(tmp_path):
    lock = module_catalog(tmp_path)
    raw = niagara_raw()
    raw["niagara"]["emitters"][0]["stacks"][2]["modules"][0]["script"] = CURRENT

    document, _, _ = document_from_raw(raw, schema=lock)

    assert module_lines(document)["initialize_particle"].startswith(CURRENT)
    # A script only one record answers to keeps its readable short name.
    assert module_lines(document)["spawn_rate"].startswith("SpawnRate")
    assert module_diagnostics(document, lock) == []


def test_a_superseded_script_is_written_in_full_while_the_current_one_stays_short(tmp_path):
    lock = module_catalog(tmp_path, deprecated=(SUPERSEDED,))
    raw = niagara_raw()
    raw["niagara"]["emitters"][0]["stacks"][2]["modules"][0]["script"] = SUPERSEDED

    document, _, _ = document_from_raw(raw, schema=lock)
    assert module_lines(document)["initialize_particle"].startswith(SUPERSEDED)

    raw["niagara"]["emitters"][0]["stacks"][2]["modules"][0]["script"] = CURRENT
    current, _, _ = document_from_raw(raw, schema=lock)
    assert module_lines(current)["initialize_particle"].startswith("InitializeParticle(")
    assert module_diagnostics(current, lock) == []


def test_linting_text_with_an_ambiguous_short_name_reports_it_per_line(tmp_path):
    """The ambiguity is a diagnostic on that line, so one module cannot fail the whole asset."""
    lock = module_catalog(tmp_path)
    text = emit(document_from_raw(niagara_raw(), schema=lock)[0])
    for path in (CURRENT, SUPERSEDED):
        text = text.replace(path, "InitializeParticle")
    document, sink = parse(text)
    assert sink.items == []

    diagnostics = lint_document(document, "niagara_system", lock, "NS_Rain.ns.nexus").items

    reported = [item for item in diagnostics if item.code == "ambiguous_module"]
    assert len(reported) == 1
    assert "InitializeParticle.InitializeParticle" in reported[0].message


def test_adding_an_ambiguous_module_needs_the_full_path(tmp_path):
    lock = module_catalog(tmp_path)
    text = emit(document_from_raw(niagara_raw(), schema=lock)[0])
    base, sink = parse(text)
    assert sink.items == []

    def planned(module: str):
        document, diagnostics = parse(text.replace("[stack Sparks/ParticleUpdate]\n", f"[stack Sparks/ParticleUpdate]\nextra : {module}(Lifetime=1)\n"))
        assert diagnostics.items == []
        return build_plan(document, base, "niagara_system", dict(), lock)

    ambiguous = planned("InitializeParticle")
    assert [item.code for item in ambiguous.diagnostics] == ["ambiguous_module"]
    assert ambiguous.verbs == []

    resolved = planned(CURRENT)
    added = next(verb for verb in resolved.verbs if verb.op == "ns_module_add")
    assert added.args["id"] == "extra"
    assert added.args["script"] == CURRENT
