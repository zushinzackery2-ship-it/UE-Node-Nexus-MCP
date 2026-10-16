"""User input links must produce executable typed verbs, preserving stack identity."""

import pytest

from ue_node_nexus_mcp.transcode.diff.niagara import diff_niagara
from ue_node_nexus_mcp.transcode.diff.plan import AssetPlan
from ue_node_nexus_mcp.transcode.text.parser import parse


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


@pytest.mark.parametrize("marker", ["@dynamic", "@link(Emitter.Rate)", "@link(Particles.Rate)",
                                    "@link(User.)", "@link(User.Rain..Rate)", "@link(User.Rain.)"])
def test_new_unsupported_or_malformed_marker_never_becomes_a_constant(marker):
    result = plan(document(marker), document())
    assert result.has_errors
    assert not result.verbs


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
