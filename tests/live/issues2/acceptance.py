"""Lifecycle boundaries checked against the isolated editor and native receipts."""

import json
from pathlib import Path

from ue_node_nexus_mcp.transcode.text.emitter import emit
from ue_node_nexus_mcp.transcode.text.model import Decl, Link
from ue_node_nexus_mcp.transcode.text.parser import parse
from .workflow import PREFIX


def raw(workflow, asset, *, stubs=False):
    output = Path(workflow.repository) / "live-readback"
    result = workflow.session.require("transcode_export", asset_paths=[asset], out_dir=str(output), include_stubs=stubs)
    assert len(result["assets"]) == 1, result
    return json.loads(Path(result["assets"][0]["file"]).read_text(encoding="utf-8"))


def publish_document(workflow, document, name):
    workflow.file(name).write_text(emit(document), encoding="utf-8")
    workflow.call("commit", all=True, message="Verify " + name + " contract editing")
    return workflow.published(workflow.call("push", allow_delete=True))


def blueprint_edits(workflow):
    name = "BP_Main.bp.nexus"
    document, sink = parse(workflow.file(name).read_text(encoding="utf-8"))
    assert not sink.has_errors
    graph = next(section for section in document.sections if section.name == "graph" and section.args == "EventGraph")
    branch = next(link for link in graph.links() if link.src == "split" and link.src_pin == "else")
    branch.dst = "hide"
    publish_document(workflow, document, name)
    workflow.compile("BP_Main")
    assert workflow.call("push")["applied"] == 0

    document, sink = parse(workflow.file(name).read_text(encoding="utf-8"))
    assert not sink.has_errors
    graph = next(section for section in document.sections if section.name == "graph" and section.args == "EventGraph")
    graph.entries = [entry for entry in graph.entries if not (
        isinstance(entry, Decl) and entry.id == "pass_mesh" or
        isinstance(entry, Link) and "pass_mesh" in (entry.src, entry.dst))]
    graph.entries.append(Link("mesh_ref", None, "hide", "self"))
    document.sections = [section for section in document.sections
                         if section.name not in ("interfaces", "dispatchers")
                         and not (section.name == "function" and section.args.startswith("Query("))]
    publish_document(workflow, document, name)
    workflow.compile("BP_Main")
    exported = raw(workflow, PREFIX + "BP_Main.BP_Main")["blueprint"]
    assert not exported["interfaces"] and not exported["dispatchers"]
    assert not any(graph["name"] == "Query" for graph in exported["graphs"])
    assert workflow.call("push")["applied"] == 0


def published_receipt_is_protected(workflow):
    published = next(entry["result"] for entry in workflow.results
                     if entry.get("result", dict()).get("status") == "published")
    apply_id = next(row["apply_id"] for row in published["rows"] if row.get("apply_id"))
    result = workflow.session.call("transcode_recover", dict(
        apply_id=apply_id, repository=workflow.repository, restore=True))
    workflow.session.report("published-receipt-protected", result)
    assert result.get("error", dict()).get("code") == "published_history", result


def delete_asset(workflow):
    path = workflow.file("MF_Retry.mf.nexus")
    assert path.exists()
    path.unlink()
    workflow.call("commit", all=True, delete=True, allow_delete=True, message="Verify asset deletion remains absent")
    result = workflow.published(workflow.call("push", allow_delete=True))
    deleted = next(row for row in result["rows"] if row["asset"] == PREFIX + "MF_Retry.MF_Retry")
    record = workflow.call("show", apply_id=deleted["apply_id"])
    assert record["receipt"]["after"]["exists"] is False
    assert not (workflow.session.project.parent / "Content/NexusIssues2/MF_Retry.uasset").exists()
    assert workflow.call("push")["applied"] == 0


def stub_stability(workflow, *, cold=False):
    asset = PREFIX + "T_Stub.T_Stub"
    evidence = workflow.session.logs / "Issues2-stub-tags.json"
    if not cold:
        workflow.session.require("asset_duplicate", source_asset_path="/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture",
                                 destination_asset_path=asset, dry_run=False, save=True)
    before = raw(workflow, asset, stubs=True)
    workflow.session.require("texture_summary_get", asset_path=asset)
    after = raw(workflow, asset, stubs=True)
    baseline = json.loads(evidence.read_text(encoding="utf-8")) if cold else None
    workflow.session.report("stub-stability", dict(
        cold=cold, before=before, after=after, baseline_tags=baseline))
    assert before["kind"] == "stub" and before["tags"] == after["tags"]
    assert before["saved_hash"] == after["saved_hash"]
    if cold:
        assert before["tags"] == baseline
    else:
        evidence.write_text(json.dumps(before["tags"], sort_keys=True), encoding="utf-8")
