"""Verify early native duplicate rejection and commandlet classification on an owned host."""

import argparse
import json
from pathlib import Path
import subprocess
import time

from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.processes import is_alive
from ue_node_nexus_mcp.instances.session.binding import EditorSession
from .host import ROOT


def launch(executable, project, arguments, log):
    command = [str(executable), str(project), "-unattended", "-nosplash", "-nosound", "-nop4",
               "-abslog=" + str(log), *arguments]
    started = time.monotonic()
    with log.with_suffix(".console.log").open("wb") as output:
        process = subprocess.Popen(command, cwd=str(project.parent), stdout=output, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        code = process.wait(timeout=180)
    return dict(executable=str(executable), arguments=arguments, pid=process.pid, exit_code=code,
                elapsed_seconds=time.monotonic() - started, log=str(log))


def run(root):
    root = root.resolve()
    root.relative_to((ROOT / "build").resolve())
    project = root.parent / "NexusValidation.uproject"
    assert project.is_file()
    session = EditorSession(BrokerClient(root, str(root.parent)), str(project))
    report = dict(project=str(project), cases=[])
    try:
        original = session.ensure(dict(dry_run=False))["instance"]
        assert original["project_key"] == project_identity(project)["project_key"]
        binaries = Path(original["executable"]).parent
        for filename in ("UnrealEditor.exe", "UnrealEditor-Cmd.exe"):
            result = launch(binaries / filename, project, ["-RenderOffscreen", "-d3d12"],
                            root.parent / (filename + "-duplicate.log"))
            report["cases"].append(result)
            assert result["exit_code"] == 73, result
            output = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
            assert "duplicate_project" in output and "key=" + original["project_key"] in output
            assert "Nexus Guard phase=PostConfigInit" not in output
            assert is_alive(original)
        empty = root.parent / "Content" / "CommandletEmpty"
        empty.mkdir(exist_ok=True)
        result = launch(binaries / "UnrealEditor-Cmd.exe", project,
                        ["-run=ResavePackages", "-ProjectOnly", "-SkipSave", "-PackageFolder=" + str(empty), "-NullRHI"],
                        root.parent / "commandlet.log")
        report["cases"].append(result)
        assert result["exit_code"] == 0, result
        output = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
        assert "duplicate_project" not in output and "Nexus Guard phase=PostConfigInit" not in output
        assert is_alive(original)
        current = session.status()
        assert current["instance_id"] == original["instance_id"] and current["state"] == "READY", current
        report.update(ok=True, original_identity=dict(instance_id=original["instance_id"], pid=original["pid"],
                                                     process_created=original["process_created"]))
        print(json.dumps(report), flush=True)
    finally:
        session.close()
        (root.parent / "startup-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    run(parser.parse_args().runtime)
