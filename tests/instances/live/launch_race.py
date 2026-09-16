"""Race ordinary editors outside Broker, then drain before changing launch profile."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import time

from ue_node_nexus_mcp.bridge import UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from .host import prepare
from .protection import close_clean, connect, require, wait_state


def ordinary(executable, project, root, index):
    log = project.parent / ("ordinary-" + str(index) + ".log")
    command = [str(executable), str(project), "-d3d12", "-RenderOffscreen", "-unattended", "-nosplash",
               "-nosound", "-NoVSync", "-nop4", "-NexusRuntime=" + str(root), "-abslog=" + str(log)]
    started = time.monotonic()
    with log.with_suffix(".console.log").open("wb") as output:
        process = subprocess.Popen(command, cwd=str(project.parent), stdout=output, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    return dict(process=process, log=log, launched_at=started)


def race(project, engine, root, session):
    executable = Path(engine) / "Engine/Binaries/Win64/UnrealEditor.exe"
    with ThreadPoolExecutor(max_workers=2) as pool:
        launches = list(pool.map(lambda index: ordinary(executable, project, root, index), range(2)))
    assert abs(launches[0]["launched_at"] - launches[1]["launched_at"]) < 1
    deadline = time.monotonic() + 120
    while all(item["process"].poll() is None for item in launches) and time.monotonic() < deadline:
        time.sleep(0.1)
    survivors = [item for item in launches if item["process"].poll() is None]
    assert len(survivors) == 1, [item["process"].returncode for item in launches]
    winner = survivors[0]
    loser = next(item for item in launches if item is not winner)
    assert loser["process"].returncode == 73
    key = project_identity(project)["project_key"]
    log = loser["log"].read_text(encoding="utf-8", errors="replace")
    assert "duplicate_project" in log and "key=" + key in log
    assert "Nexus Guard phase=PostConfigInit" not in log
    result = connect(session, winner["process"])
    original = result["instance"]
    assert original["ownership"] == "external" and original["pid"] == winner["process"].pid
    session.call("adopt", dict(instance_id=original["instance_id"], process_created=original["process_created"],
                               project_path=str(project), reason="owned simultaneous startup fixture", protected=False, dry_run=False))
    final = close_clean(session, original["instance_id"])
    assert final["state"] == "EXITED" and final["exit_code"] == 0
    assert winner["process"].wait(timeout=5) == 0
    return dict(winner=original, final=final, rejected_pid=loser["process"].pid, rejected_exit_code=73,
                launch_spacing_seconds=abs(launches[0]["launched_at"] - launches[1]["launched_at"]))


def run(name, engine):
    from ue_node_nexus_mcp.instances.session.binding import EditorSession

    project = prepare(name)
    root = project.parent / "Runtime"
    root.mkdir()
    session = EditorSession(BrokerClient(root, str(project.parent)), str(project))
    report = dict(project=str(project))
    try:
        report["race"] = race(project, engine, root, session)
        session.ensure(dict(mode="reuse_or_start", engine_path=engine, launch_profile="offscreen", rhi="d3d12", dry_run=False))
        first = wait_state(session, ("READY",), 240)
        bridge = UeBridgeClient(instances=session)
        report["builds"] = require(bridge, "bridge_capabilities_get")["build"]
        try:
            session.ensure(dict(mode="reuse_or_start", launch_profile="interactive", dry_run=False))
        except InstanceError as error:
            assert error.code == "instance_incompatible", error.envelope()
            report["display_conflict"] = error.envelope()
        else:
            raise AssertionError("display mode changed without draining the existing process")
        assert is_alive(first)
        closed = close_clean(session, first["instance_id"])
        assert closed["exit_code"] == 0 and not is_alive(first), closed
        session.ensure(dict(mode="reuse_or_start", engine_path=engine, launch_profile="interactive", rhi="d3d12", dry_run=False))
        second = wait_state(session, ("READY",), 240)
        assert second["instance_id"] != first["instance_id"] and second["launch_profile"] == "interactive"
        assert second["protected"] and second["executable"].lower().endswith("/unrealeditor.exe")
        require(bridge, "project_context_get")
        final = close_clean(session, second["instance_id"])
        assert final["exit_code"] == 0 and final["state"] == "EXITED", final
        report.update(ok=True, offscreen=first, offscreen_exit=closed, interactive=second, final=final)
        print(json.dumps(dict(ok=True, race_exit_codes=[0, 73], profile_exit_codes=[closed["exit_code"], final["exit_code"]])), flush=True)
    finally:
        session.close()
        (project.parent / "launch-race-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    run(args.name, args.engine)
