"""Exercise external ownership, real dirty packages and save-failure shutdown gates."""

import argparse
import json
from pathlib import Path
import stat
import subprocess
import time
from types import SimpleNamespace
import uuid

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.identity.discovery import arguments
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.watch import ProcessWatch
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from .host import ROOT, prepare


def wait_state(session, expected, timeout=75):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = session.status()
        if state["state"] in expected:
            return state
        time.sleep(0.5)
    raise AssertionError(state)


def close_clean(session, identifier):
    deadline = time.monotonic() + 240
    while True:
        preview = session.call("close", dict(instance_id=identifier, dry_run=True))
        if not preview["blockers"]:
            break
        assert time.monotonic() < deadline, preview
        time.sleep(2)
    session.call("close", dict(instance_id=identifier, dry_run=False))
    return wait_state(session, ("BLOCKED", "EXITED"))


def require(bridge, operation, **payload):
    response = bridge.call(operation, payload)
    assert response.get("ok"), response
    return response["data"]


def save_asset(bridge, asset):
    deadline = time.monotonic() + 45
    while True:
        response = bridge.call("asset_save", dict(asset_path=asset, dry_run=False))
        if response.get("ok"):
            return
        assert response.get("error", dict()).get("code") == "save_blocked_asset_streaming_suspended", response
        assert time.monotonic() < deadline, response
        time.sleep(1)


def connect(session, process):
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        assert process.poll() is None, process.returncode
        try:
            result = session.ensure(dict(dry_run=False))
            if result["instance"]["state"] in ("READY", "IDLE"):
                return result
        except InstanceError as error:
            assert error.code in ("instance_unverified", "instance_missing", "instance_starting"), error.envelope()
        time.sleep(1)
    raise AssertionError("external fixture did not become ready")


def dirty_protection(session, report):
    bridge = UeBridgeClient(instances=session)
    for package in session.status().get("dirty_packages", []):
        if package.startswith("/Game/InstanceAcceptance/"):
            save_asset(bridge, package + "." + package.rsplit("/", 1)[1])
    suffix = uuid.uuid4().hex[:8]
    parent = f"/Game/InstanceAcceptance/M_Base_{suffix}.M_Base_{suffix}"
    asset = f"/Game/InstanceAcceptance/MI_Dirty_{suffix}.MI_Dirty_{suffix}"
    package_name = asset.split(".")[0]
    require(bridge, "asset_create", asset_path=parent, asset_kind="material", dry_run=False, save=False)
    require(bridge, "asset_compile", asset_path=parent)
    save_asset(bridge, parent)
    require(bridge, "asset_create", asset_path=asset, asset_kind="material_instance", dry_run=False, save=False,
            parent_asset_path=parent)
    save_asset(bridge, asset)
    require(bridge, "material_instance_params_set", asset_path=asset, params=[], dry_run=False)
    identifier = session.current()["instance_id"]
    session.call("close", dict(instance_id=identifier, dry_run=False))
    dirty = wait_state(session, ("BLOCKED", "EXITED"))
    assert dirty["state"] == "BLOCKED" and package_name in dirty["dirty_packages"], dirty
    report["dirty_asset"] = dirty
    path = Path(dirty["project_path"]).parent / "Content" / (package_name.removeprefix("/Game/") + ".uasset")
    assert path.is_file()
    path.chmod(stat.S_IREAD)
    try:
        session.ensure(dict(dry_run=False))
        session.call("close", dict(instance_id=identifier, dry_run=False, save_packages=[package_name]))
        failed = wait_state(session, ("BLOCKED", "EXITED"))
        assert failed["state"] == "BLOCKED" and package_name in failed["failed_packages"], failed
        assert "save_failed" in failed["blockers"] and is_alive(failed)
        report["read_only_save"] = failed
    finally:
        path.chmod(stat.S_IREAD | stat.S_IWRITE)
        session.ensure(dict(dry_run=False))
        save_asset(bridge, asset)

    require(bridge, "level_actor_spawn", class_path="/Script/Engine.Actor", label="NexusOwnedDirtyMapFixture", dry_run=False)
    session.call("close", dict(instance_id=identifier, dry_run=False))
    dirty_map = wait_state(session, ("BLOCKED", "EXITED"))
    assert dirty_map["state"] == "BLOCKED" and dirty_map["dirty_packages"], dirty_map
    assert package_name not in dirty_map["dirty_packages"], dirty_map
    report["dirty_map"] = dirty_map
    session.ensure(dict(dry_run=False))
    # Discard only the actor created above in this isolated, freshly prepared project.
    current = require(bridge, "level_current_get")["package_name"]
    target = "/Engine/Maps/Templates/Template_Default" if current == "/Engine/Maps/Entry" else "/Engine/Maps/Entry"
    require(bridge, "level_open", map_path=target, dry_run=False, discard_changes=True)


def run(name, engine, resume=False):
    project = (ROOT / "build" / name / "NexusValidation.uproject").resolve() if resume else prepare(name)
    project.relative_to((ROOT / "build").resolve())
    root = project.parent / "Runtime"
    root.mkdir(exist_ok=True)
    executable = Path(engine) / "Engine/Binaries/Win64/UnrealEditor.exe"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    output = (project.parent / "external-console.log").open("ab")
    watch = None
    if resume:
        prior = json.loads((project.parent / "protection-result.json").read_text())
        identity = json.loads((root / "Instances" / (str(prior["external_pid"]) + ".json")).read_text())
        assert is_alive(identity) and identity["project_key"] == project_identity(project)["project_key"]
        assert str(project) in arguments(identity["pid"])
        watch = ProcessWatch(identity)
        process = SimpleNamespace(pid=identity["pid"], poll=watch.poll)
    else:
        process = subprocess.Popen([str(executable), str(project), "-d3d12", "-nosplash", "-nosound", "-NoVSync",
                                    "-ini:EditorPerProjectUserSettings:[/Script/UnrealEd.EditorLoadingSavingSettings]:bAutoSaveEnable=False",
                                    "-NexusRuntime=" + str(root), "-abslog=" + str(project.parent / "external.log")],
                                   cwd=str(project.parent), stdout=output, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    report = dict(prior) if resume else dict(project=str(project), external_pid=process.pid)
    try:
        result = connect(session, process)
        external = result["instance"]
        assert external["pid"] == process.pid and external["launch_profile"] == "interactive", external
        if not report.get("external_preserved_seconds"):
            assert external["ownership"] == "external" and external["protected"]
            report["external"] = external
            session.release()
            started = time.monotonic()
            while time.monotonic() - started < 145:
                assert process.poll() is None
                observed = session.status()
                assert observed["ownership"] == "external" and observed["use_count"] == 0, observed
                time.sleep(5)
            report["external_preserved_seconds"] = time.monotonic() - started
            print(json.dumps(dict(event="external_preserved", pid=process.pid)), flush=True)
        if external["ownership"] == "external":
            session.call("adopt", dict(instance_id=external["instance_id"], process_created=external["process_created"],
                                       project_path=str(project), reason="owned isolated acceptance fixture", protected=False, dry_run=False))
        dirty_protection(session, report)
        final = close_clean(session, external["instance_id"])
        assert final["state"] == "EXITED" and final["exit_code"] == 0, final
        assert final["use_count"] == final["scope_count"] == 0
        report.update(ok=True, final=final)
        print(json.dumps(dict(ok=True, external_preserved=True, dirty_asset=True, dirty_map=True, save_failure=True)), flush=True)
    finally:
        session.close()
        output.close()
        if watch:
            watch.close()
        (project.parent / "protection-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    run(args.name, args.engine, args.resume)
