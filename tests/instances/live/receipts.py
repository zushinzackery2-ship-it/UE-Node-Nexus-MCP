"""Lose actual saved native responses, restart MCP, and recover without replaying writes."""

import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

import ue_node_nexus_mcp
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.transcode.collaboration.store import Store
from ue_node_nexus_mcp.transcode.paths import text_path
from .collaboration import edit, wait_state
from .host import ROOT, prepare
from .stdio import connect


async def seed(peer, mapping):
    asset = "/Game/InstanceReceipts/M_Lost.M_Lost"
    initialized = await peer.sync("init", pull_all=False)
    path = text_path(Path(mapping["mirror_root"]) / mapping["mirror_project_name"], asset, "material")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"nexus: 1\nasset: {asset}\nclass: Material\nschema: {initialized['schema_key']}\n\n"
                    "[graph]\nc : Constant(R=0.25) @ 0,0\nc -> out.BaseColor\n", encoding="utf-8")
    await peer.sync("push", paths=[asset])
    workspace = await peer.sync("checkout", paths=[asset], agent_id="receipt-owner")
    edit(workspace, asset, "R=0.25", "R=0.5")
    await peer.sync("commit", workspace, all=True, message="Saved apply with lost responses")
    return asset, workspace


async def run(name, engine):
    project = prepare(name)
    root = project.parent / "Runtime"
    controller = BrokerClient(root, str(project.parent))
    trace = project.parent / "transport-events.jsonl"
    environment = dict(PYTHONPATH=str(ROOT), NEXUS_TEST_RESPONSE_TRACE=str(trace), NEXUS_TEST_DROP_RESPONSES="1")
    module = "tests.instances.support.interrupted_server"
    report = dict(project=str(project), python=sys.executable, package_path=ue_node_nexus_mcp.__file__)
    manager = None
    try:
        async with connect(project, root, project.parent / "Workspaces/Before", module, environment) as peer:
            acquired = await peer.execute("bridge_instance_ensure", mode="reuse_or_start", engine_path=engine, dry_run=False)
            original = await wait_state(controller, acquired["instance"]["instance_id"], ("READY",))
            await peer.execute("bridge_instance_status", instance_id=original["instance_id"])
            report["builds"] = (await peer.execute("bridge_capabilities_get"))["build"]
            manager = json.loads((root / "manager.json").read_text())["identity"]
            asset, workspace = await seed(peer, acquired["instance"])
            interrupted = await peer.sync("push", workspace, paths=[asset], allow_error=True)
            failure = interrupted["errors"][asset]
            assert failure["code"] == "operation_outcome_unknown", interrupted
            apply_id = failure["details"]["apply_id"]
            store = Store(Path(acquired["instance"]["repository"]))
            before = store.record("apply", apply_id)
            assert before["phase"] == "recovery_required" and not before.get("published"), before
            assert is_alive(original)
            report.update(asset=asset, workspace=workspace, apply_id=apply_id, interrupted=interrupted, before=before)
        environment["NEXUS_TEST_DROP_RESPONSES"] = "0"
        async with connect(project, root, project.parent / "Workspaces/After", module, environment) as peer:
            acquired = await peer.execute("bridge_instance_ensure", dry_run=False)
            assert acquired["instance"]["instance_id"] == original["instance_id"]
            recovered = await peer.sync("recover", workspace, apply_id=apply_id)
            assert recovered["phase"] == "completed" and recovered["published"], recovered
            again = await peer.sync("recover", workspace, apply_id=apply_id)
            assert again["published"] == recovered["published"]
            record = Store(store.root).record("apply", apply_id)
            assert record["phase"] == "completed" and record["receipt"]["phase"] == "ue_committed"
            await peer.sync("pull", workspace, paths=[asset])
            assert "R=0.5" in Path(workspace["file_paths"][asset]).read_text(encoding="utf-8")
            events = [json.loads(line) for line in trace.read_text().splitlines()]
            applies = [item for item in events if item["operation"] == "transcode_apply"]
            assert len(applies) == 1 and applies[0]["apply_id"] == apply_id, events
            assert applies[0]["native_phase"] == "ue_committed" and applies[0]["response_dropped"]
            assert sum(item["response_dropped"] for item in events) == 2, events
            await peer.execute("bridge_instance_release")
            preview = await asyncio.to_thread(controller.call, "close", dict(instance_id=original["instance_id"], dry_run=True))
            assert not preview["blockers"], preview
            await asyncio.to_thread(controller.call, "close", dict(instance_id=original["instance_id"], dry_run=False))
            final = await wait_state(controller, original["instance_id"], ("EXITED",), 75)
            assert final["exit_code"] == 0 and final["scope_count"] == final["use_count"] == 0, final
            report.update(ok=True, recovered=recovered, transport_events=events, final=final)
            print(json.dumps(dict(ok=True, apply_id=apply_id, actual_apply_requests=len(applies), exit_code=0)), flush=True)
    finally:
        controller.close()
        if report.get("ok") and manager:
            deadline = time.monotonic() + 80
            while is_alive(manager) and time.monotonic() < deadline:
                await asyncio.sleep(2)
            report["manager_exited"] = not is_alive(manager)
            report["ok"] = report["manager_exited"]
        (project.parent / "receipts-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    assert report.get("ok"), report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    asyncio.run(run(args.name, args.engine))
