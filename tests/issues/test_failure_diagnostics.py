"""Issue 14: a failed publication reports the messages that explain it.

A rolled back apply answers with a fresh error envelope, so the body's own data -
including every compiler message naming the node that was wrong - is gone by the
time the caller sees anything. Losing them turns a precise failure into "it did
not work", and the only way forward is to guess.
"""

from __future__ import annotations

import pytest

from ue_node_nexus_mcp.transcode.collaboration.apply.transactions import diagnostics
from ue_node_nexus_mcp.transcode.sync_project import SyncError

from tests.collaboration.test_recovery import MATERIAL, call, edit, project  # noqa: F401

MESSAGE = dict(severity="error", code="pin_not_found", message="Constant.R has no pin named Q", asset=MATERIAL)


def test_messages_are_taken_from_whichever_place_the_failure_carried_them():
    assert diagnostics(dict(data=dict(diagnostics=[MESSAGE])), None) == [MESSAGE]
    assert diagnostics(dict(), dict(response_data=dict(diagnostics=[MESSAGE]))) == [MESSAGE]
    assert diagnostics(dict(), dict(response=dict(data=dict(diagnostics=[MESSAGE])))) == [MESSAGE]
    # The live body wins over the receipt's copy of the same run.
    other = dict(MESSAGE, code="stale")
    assert diagnostics(dict(data=dict(diagnostics=[MESSAGE])), dict(response_data=dict(diagnostics=[other]))) == [MESSAGE]


@pytest.mark.parametrize("response,receipt", [
    (dict(), None),
    (dict(data=None), dict(response_data=None)),
    (dict(data=dict(diagnostics=[])), dict(response_data=dict(diagnostics=[]))),
    (dict(data="not an object"), dict(response_data="not an object")),
])
def test_a_failure_that_carried_no_messages_reports_none_rather_than_breaking(response, receipt):
    assert diagnostics(response, receipt) == []


def test_a_rolled_back_apply_hands_the_compiler_messages_to_the_caller(project):  # noqa: F811
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")

    def rolled_back(operation, payload, **options):
        response = ue.call(operation, payload, **options)
        if operation != "transcode_apply" or payload.get("dry_run"):
            return response
        # What UE answers when it refuses the plan and restores the package: a
        # fresh error envelope whose own data is gone, with the run's messages
        # left only on the durable receipt.
        receipt = (response.get("data") or dict()).get("receipt") or dict()
        return dict(ok=False, operation=operation, request_id=response.get("request_id"),
                    error=dict(code="compile_failed", message="asset did not compile"),
                    data=dict(receipt=dict(receipt, phase="rolled_back",
                                           response_data=dict(diagnostics=[MESSAGE]))))

    result = call(project, "push", [MATERIAL], bridge=rolled_back, stop_on_error=False)
    failure = result["errors"][MATERIAL]
    assert failure["code"] == "compile_failed"
    assert failure["details"]["diagnostics"] == [MESSAGE]
    assert failure["details"]["phase"] == "rolled_back"


def test_the_repository_stops_trusting_memory_it_could_not_write(project):  # noqa: F811
    ue, env, workspace, store = project
    edit(project, MATERIAL, "Constant(R=0.000001)", "Constant(R=0.04)")
    call(project, "commit", all=True, message="A")

    def refuse(operation, payload, **options):
        response = ue.call(operation, payload, **options)
        if operation != "transcode_apply" or payload.get("dry_run"):
            return response
        receipt = (response.get("data") or dict()).get("receipt") or dict()
        return dict(ok=False, operation=operation, request_id=response.get("request_id"),
                    error=dict(code="compile_failed", message="asset did not compile"),
                    data=dict(receipt=dict(receipt, phase="rolled_back")))

    call(project, "push", [MATERIAL], bridge=refuse, stop_on_error=False)
    remembered = store.record("memory", "observed") or dict(entries=dict())
    assert MATERIAL not in remembered["entries"]


def test_a_publication_blocked_before_it_starts_still_explains_itself(project):  # noqa: F811
    ue, env, workspace, store = project
    with pytest.raises(SyncError) as failure:
        call(project, "push", ["/Game/Nope.Nope"])
    assert failure.value.code
    assert str(failure.value)
