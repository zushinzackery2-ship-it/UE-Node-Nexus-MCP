"""Rejected creations and edits restore their durable native checkpoints."""

from copy import deepcopy

from .workflow import PREFIX


def reject_creation(workflow):
    asset = PREFIX + "M_Readback.M_Readback"
    path = workflow.file("M_Readback.mat.nexus")
    path.write_text("nexus: 1\nasset: " + asset + "\nclass: Material\n"
                    "[graph]\namount : Constant(R=0.5) @ 0,0\namount -> out.BaseColor\n", encoding="utf-8")
    workflow.call("commit", all=True, message="Verify rejected new asset restoration")

    def altered(operation, payload):
        response = workflow.session.call(operation, payload)
        if operation == "transcode_apply" and payload.get("asset_path") == asset and response.get("ok"):
            response = deepcopy(response)
            receipt = response["data"]["receipt"]
            node = next(row for row in receipt["after"]["graph"]["nodes"] if row["class_short"] == "Constant")
            next(row for row in node["props"] if row["name"] == "R")["value"] = "999"
        return response

    workflow.bridge = altered
    try:
        result = workflow.call("push")
    finally:
        workflow.bridge = workflow.session.call
    error = result.get("errors", dict()).get(asset, dict())
    assert error.get("code") == "apply_result_mismatch", (error.get("code"), str(workflow.session.logs))
    assert error["details"].get("phase") == "rolled_back", (error["details"].get("recovery_error"), str(workflow.session.logs))
    assert not (workflow.session.project.parent / "Content/NexusIssues2/M_Readback.uasset").exists()
    workflow.published(workflow.call("push"))
    workflow.compile("M_Readback")
