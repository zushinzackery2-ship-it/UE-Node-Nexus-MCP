"""Namespace input links produce typed verbs and preserve native stack identity."""

import pytest

from ue_node_nexus_mcp.transcode.diff.niagara import diff_niagara
from ue_node_nexus_mcp.transcode.diff.plan import AssetPlan
from ue_node_nexus_mcp.transcode.text.parser import parse
from ue_node_nexus_mcp.transcode.raw.niagara import input_value_text


ASSET = "/Game/Weather/NS_Rain"


def document(value="8000", user="", module=True):
    text = f"nexus: 1\nasset: {ASSET}\nclass: NiagaraSystem\nschema: test\n"
    text += "[emitter Rain]\nEnabled = true\n"
    if user:
        text += "[user]\n" + user + "\n"
    text += "[stack Rain/EmitterUpdate]\n"
    if module:
        text += f"spawn : SpawnRate(SpawnRate={value}, Spawn Probability=1) @ 100,200\n"
    result, sink = parse(text)
    assert not [d for d in sink.items if d.severity == "error"]
    return result


def plan(local, base):
    result = AssetPlan(ASSET, "niagara_system")
    diff_niagara(local, base, result, "niagara_system")
    return result


def test_constant_to_user_link_creates_user_before_binding_and_preserves_module():
    before = document()
    before.section("stack", "Rain/EmitterUpdate").decls()[0].meta["guid"] = "existing-guid"
    after = document("@link(User.SpawnRate)", "SpawnRate : float = 8000")
    result = plan(after, before)
    assert not result.has_errors, result.summary()
    assert [v.op for v in result.verbs] == ["ns_user_param_add", "ns_module_input_link"]
    assert result.verbs[0].args == dict(name="SpawnRate", type="float", value="8000")
    assert result.verbs[1].args == dict(emitter="Rain", group="EmitterUpdate", id="spawn",
                                       input="SpawnRate", parameter="User.SpawnRate")
    assert result.verbs[1].line == after.section("stack", "Rain/EmitterUpdate").decls()[0].line
    assert before.section("stack", "Rain/EmitterUpdate").decls()[0].meta["guid"] == "existing-guid"


def test_new_module_is_created_before_its_link():
    result = plan(document("@link(User.SpawnRate)", "SpawnRate : float = 8000"), document(module=False))
    assert not result.has_errors, result.summary()
    assert [v.op for v in result.verbs] == ["ns_user_param_add", "ns_module_add", "ns_module_input_link", "ns_module_input_set"]
    assert result.verbs[1].args["id"] == result.verbs[2].args["id"] == "spawn"
    assert result.verbs[3].args["input"] == "Spawn Probability"


def test_existing_identical_user_link_is_a_noop():
    bound = document("@link(User.SpawnRate)", "SpawnRate : float = 8000")
    result = plan(document("@link(User.SpawnRate)", "SpawnRate : float = 8000.0"), bound)
    assert not result.has_errors and result.empty


def test_relink_changes_only_the_input_binding():
    user = "SpawnRate : float = 8000\nOtherRate : float = 3000"
    result = plan(document("@link(User.OtherRate)", user), document("@link(User.SpawnRate)", user))
    assert not result.has_errors
    assert len(result.verbs) == 1 and result.verbs[0].op == "ns_module_input_link"
    assert result.verbs[0].args["parameter"] == "User.OtherRate"


@pytest.mark.parametrize("parameter", ["User.SpawnRate", "User.Rain.Rate", "User._Rate2"])
def test_user_handle_is_forwarded_without_parsing_as_a_float(parameter):
    result = plan(document(f"@link({parameter})"), document())
    assert not result.has_errors
    assert result.verbs[0].args["parameter"] == parameter
    assert "value" not in result.verbs[0].args


@pytest.mark.parametrize("marker", ["@dynamic", "@link(Position)", "@link(Particles..Position)",
                                    "@link(User.)", "@link(User.Rain..Rate)", "@link(User.Rain.)"])
def test_new_unsupported_or_malformed_marker_never_becomes_a_constant(marker):
    result = plan(document(marker), document())
    assert result.has_errors
    assert not result.verbs


@pytest.mark.parametrize("parameter", ["Emitter.Rate", "Particles.Position", "Particles.Velocity",
                                      "Output.SampleVectorField.SampledVector", "Engine.DeltaTime", "System.Age"])
def test_native_namespace_link_is_forwarded_for_stack_type_validation(parameter):
    result = plan(document(f"@link({parameter})"), document())
    assert not result.has_errors, result.summary()
    assert [verb.op for verb in result.verbs] == ["ns_module_input_link"]
    assert result.verbs[0].args["parameter"] == parameter


def test_explicit_native_conversion_is_distinct_from_a_direct_link():
    result = plan(document("@convert(Particles.Position)"), document())
    assert not result.has_errors, result.summary()
    assert [verb.op for verb in result.verbs] == ["ns_module_input_convert"]
    assert result.verbs[0].args["parameter"] == "Particles.Position"


def test_conversion_readback_preserves_the_authored_native_cast():
    raw = dict(converted="Particles.Position", conversion_script=
               "/Niagara/DynamicInputs/Transforms/ConvertPositionToVector.ConvertPositionToVector",
               linked=None, dynamic=True, has_override=True, value="")
    assert input_value_text(raw) == "@convert(Particles.Position)"


def test_weather_sampling_adds_the_writer_before_linking_the_consumer():
    prefix = f"nexus: 1\nasset: {ASSET}\nclass: NiagaraSystem\nschema: test\n[stack Rain/ParticleUpdate]\n"
    before, before_sink = parse(prefix)
    after, after_sink = parse(prefix +
        "sample : /Niagara/VectorFields/SampleVectorField.SampleVectorField(SamplePoint=@link(Particles.Position))\n" +
        "force : /Niagara/VectorFields/ApplyVectorField.ApplyVectorField(Vector Field Vector=@link(Output.SampleVectorField.SampledVector))\n")
    assert not before_sink.has_errors and not after_sink.has_errors
    result = plan(after, before)
    assert not result.has_errors, result.summary()
    assert [(verb.op, verb.args["id"]) for verb in result.verbs] == [
        ("ns_module_add", "sample"), ("ns_module_input_link", "sample"),
        ("ns_module_add", "force"), ("ns_module_input_link", "force")]
    assert [verb.args["parameter"] for verb in result.verbs if verb.op == "ns_module_input_link"] == [
        "Particles.Position", "Output.SampleVectorField.SampledVector"]


@pytest.mark.parametrize("marker", ["@dynamic", "@link(Emitter.Rate)"])
def test_existing_opaque_marker_survives_unrelated_user_default_edit(marker):
    result = plan(document(marker, "SpawnRate : float = 4000"), document(marker, "SpawnRate : float = 8000"))
    assert not result.has_errors
    assert [v.op for v in result.verbs] == ["ns_user_param_set"]


def test_link_to_constant_retains_the_explicit_numeric_set_verb():
    user = "SpawnRate : float = 8000"
    result = plan(document("2800", user), document("@link(User.SpawnRate)", user))
    assert not result.has_errors
    assert [v.op for v in result.verbs] == ["ns_module_input_set"]
    assert result.verbs[0].args["value"] == "2800"


def test_removed_link_emits_a_native_reset_without_changing_the_user_parameter():
    user = "SpawnRate : float = 8000"
    base = document("@link(User.SpawnRate)", user)
    local = document("@link(User.SpawnRate)", user)
    local.section("stack", "Rain/EmitterUpdate").decls()[0].args.pop(0)
    base.section("stack", "Rain/EmitterUpdate").decls()[0].meta["input_defaults"] = dict(SpawnRate="10")
    result = plan(local, base)
    assert not result.has_errors
    assert [v.op for v in result.verbs] == ["ns_module_input_reset"]
    assert result.verbs[0].args["default"] == "10"


def test_user_type_replacement_is_applied_before_the_link():
    result = plan(document("@link(User.SpawnRate)", "SpawnRate : float = 8000"), document(user="SpawnRate : int = 8000"))
    assert not result.has_errors
    assert [v.op for v in result.verbs] == ["ns_user_param_remove", "ns_user_param_add", "ns_module_input_link"]
