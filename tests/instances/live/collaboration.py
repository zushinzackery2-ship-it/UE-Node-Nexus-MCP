"""Two actual MCP workspaces publish through one UE and keep working after its exit."""

import argparse
import asyncio
import json
from pathlib import Path
import sys
import time

import ue_node_nexus_mcp
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from ue_node_nexus_mcp.transcode.paths import text_path
from .host import ROOT, prepare
from .stdio import connect


def edit(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    before = path.read_text(encoding="utf-8")
    assert old in before, (old, before)
    path.write_text(before.replace(old, new), encoding="utf-8", newline="\n")


async def wait_state(controller, identifier, expected, timeout=240):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = await asyncio.to_thread(controller.call, "status", dict(instance_id=identifier))
        if state["state"] in expected:
            return state
        assert state["state"] not in ("BLOCKED", "UNRESPONSIVE"), state
        await asyncio.sleep(1)
    raise AssertionError(state)


async def publish(first, second, mapping, report):
    asset = "/Game/InstanceCollaboration/M_Main.M_Main"
    initialized = await first.sync("init", pull_all=False)
    path = text_path(Path(mapping["mirror_root"]) / mapping["mirror_project_name"], asset, "material")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"nexus: 1\nasset: {asset}\nclass: Material\nschema: {initialized['schema_key']}\n\n"
                    "[asset]\nTwoSided = true\n\n[graph]\nc : Constant(R=0.25) @ 0,0\nc -> out.BaseColor\n", encoding="utf-8")
    await first.sync("push", paths=[asset])
    a = await first.sync("checkout", paths=[asset], agent_id="workspace-A")
    b = await second.sync("checkout", paths=[asset], agent_id="workspace-B")
    assert a["id"] != b["id"] and a["files_root"] != b["files_root"]
    edit(a, asset, "R=0.25", "R=0.5")
    await first.sync("commit", a, all=True, message="A: independent value edit")
    assert (await first.sync("push", a))["status"] == "published"
    edit(b, asset, "TwoSided = True", "TwoSided = False")
    await second.sync("commit", b, all=True, message="B: independent sidedness edit")
    assert (await second.sync("push", b))["status"] == "published"
    merged = Path(b["file_paths"][asset]).read_text(encoding="utf-8")
    # Native export omits TwoSided after it returns to the schema default False.
    assert "R=0.5" in merged and "TwoSided = True" not in merged, merged
    await first.sync("pull", a)
    edit(a, asset, "R=0.5", "R=0.6")
    edit(b, asset, "R=0.5", "R=0.7")
    await first.sync("commit", a, all=True, message="A: conflicting edit")
    await second.sync("commit", b, all=True, message="B: conflicting edit")
    await first.sync("push", a)
    conflict = await second.sync("push", b, allow_error=True)
    assert conflict["status"] == "conflict", conflict
    for item in conflict["conflicts"]:
        await second.sync("resolve", b, merge_id=conflict["merge_id"], conflict_id=item["conflict_id"], choice="ours")
    assert (await second.sync("continue", b, merge_id=conflict["merge_id"]))["status"] == "published"
    report.update(asset=asset, first_workspace=a, second_workspace=b, merged_text=merged,
                  conflict_id=conflict["merge_id"], assertions=["shared repository", "independent checkouts",
                  "nonconflicting stale-copy merge", "conflict resolution and publication"])
    return asset, a, b


async def finish(second, controller, identifier, asset, workspace, report):
    mismatch = await second.execute("bridge_instance_ensure", dry_run=False, allow_error=True,
                                    mirror_root=str(second.workspace / "Content_Transcoded"))
    assert mismatch["error"]["code"] == "repository_mismatch", mismatch
    await second.execute("bridge_instance_release")
    deadline = time.monotonic() + 90
    while True:
        preview = await asyncio.to_thread(controller.call, "close", dict(instance_id=identifier, dry_run=True))
        if not preview["blockers"]:
            break
        assert time.monotonic() < deadline, preview
        await asyncio.sleep(2)
    await asyncio.to_thread(controller.call, "close", dict(instance_id=identifier, dry_run=False))
    final = await wait_state(controller, identifier, ("EXITED",), 75)
    assert final["exit_code"] == 0 and final["use_count"] == final["scope_count"] == 0, final
    edit(workspace, asset, "R=0.7", "R=0.8")
    await second.sync("lint", workspace, paths=[asset])
    await second.sync("stage", workspace, paths=[asset])
    offline = await second.sync("commit", workspace, message="offline work after editor reclamation")
    after = await asyncio.to_thread(controller.call, "list", dict(project_path=report["project"], include_exited=True))
    assert all(row["state"] == "EXITED" for row in after["instances"]), after
    assert any(row["instance_id"] == identifier for row in after["instances"]), after
    report.update(ok=True, final=final, offline_commit=offline)
    report["assertions"].extend(["conflicting explicit root rejected", "normal editor exit", "offline lint/stage/commit"])
    print(json.dumps(dict(ok=True, instance_id=identifier, assertions=report["assertions"])), flush=True)


async def run(name, engine, resume=False):
    project = (ROOT / "build" / name / "NexusValidation.uproject").resolve() if resume else prepare(name)
    project.relative_to((ROOT / "build").resolve())
    root = project.parent / "Runtime"
    controller = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    report = dict(project=str(project), python=sys.executable, package_path=ue_node_nexus_mcp.__file__)
    try:
        async with connect(project, root, project.parent / "Workspaces/B") as second:
            async with connect(project, root, project.parent / "Workspaces/A") as first:
                await first.execute("bridge_instance_list")
                manager = json.loads((root / "manager.json").read_text())["identity"]
                acquisitions = await asyncio.gather(*(peer.execute("bridge_instance_ensure", mode="reuse_or_start",
                    dry_run=False, engine_path=engine) for peer in (first, second)))
                identifier = acquisitions[0]["instance"]["instance_id"]
                assert all(row["instance"]["instance_id"] == identifier for row in acquisitions)
                assert acquisitions[0]["instance"]["repository"] == acquisitions[1]["instance"]["repository"]
                report["acquisitions"] = acquisitions
                await wait_state(controller, identifier, ("READY",))
                for peer in (first, second):
                    await peer.execute("bridge_instance_ensure", dry_run=False)
                    await peer.execute("project_context_get")
                report["builds"] = (await first.execute("bridge_capabilities_get"))["build"]
                asset, a, b = await publish(first, second, acquisitions[0]["instance"], report)
            await second.execute("project_context_get")
            assert json.loads((root / "manager.json").read_text())["identity"] == manager
            report["assertions"].append("creator MCP exited while the remaining workspace continued")
            await finish(second, controller, identifier, asset, b, report)
    finally:
        controller.close()
        (project.parent / "collaboration-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    asyncio.run(run(args.name, args.engine, args.resume))
