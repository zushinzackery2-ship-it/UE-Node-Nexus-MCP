"""Replay reducers preserve the same failure, rather than accepting any failure."""

from .shrink import reduce_actions, signature
from .sequences import generate, edit_existing


def test_seed_materializes_a_deterministic_action_trace():
    assert generate(2711, 40) == generate(2711, 40)
    assert generate(2711, 40) != generate(2712, 40)


def test_reducer_retains_trigger_order_and_immutable_baseline():
    actions, count = reduce_actions(["baseline", "noise", "link", "compile", "noise", "reset", "save"],
                                    lambda trace: "link" in trace and "reset" in trace and trace.index("link") < trace.index("reset"), 30)
    assert actions == ["baseline", "link", "reset"]
    assert count <= 30


def test_generic_exit_is_not_a_semantic_failure_oracle():
    assert signature(dict(error=dict(type="CalledProcessError", message="exit 1"))) == ()
    assert len(signature(dict(error=dict(message='{"rows":[{"code":"apply_result_mismatch"}]}')))) == 1
    left = signature(dict(error=dict(code="apply_result_mismatch", field=["R"])))
    right = signature(dict(error=dict(code="apply_result_mismatch", field=["G"])))
    assert left != right


def test_generator_can_edit_an_omitted_native_default_again():
    action = dict(value=0.125, two_sided=False)
    text = "[asset]\n[graph]\nc : Constant @ 0,0\nc -> out.EmissiveColor\n"
    edited = edit_existing("M_Sequence.mat.nexus", text, action)
    assert "c : Constant(R=0.125) @ 0,0" in edited
