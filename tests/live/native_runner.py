"""Run the native instance identity automation tests in their isolated host."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess

from tests.live.editor.session import environment

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    engine = Path(os.environ["UE_NEXUS_ENGINE_DIR"])
    host = ROOT / "build/scene-tests"
    project = host / "NexusValidation.uproject"
    output = host / "Logs/NativeAutomation"
    command = [
        str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(project),
        "-nullrhi", "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
        "-stdout", "-FullStdOutLogOutput", "-ExecCmds=Automation RunTests Nexus.Instances",
        "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + str(output),
        "-abslog=" + str(host / "Logs/NativeAutomation.log"),
    ]
    with (host / "Logs/NativeAutomation-console.log").open("w", encoding="utf-8") as stream:
        process = subprocess.run(command, env=environment(project), stdout=stream, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW, timeout=300, check=True)
    report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
    expected = set(("RemoveSwapIdentity", "RemoveShiftIdentity", "InvalidRelocationRejected", "MetadataUndoRedo"))
    names = set(test["fullTestPath"].removeprefix("Nexus.Instances.") for test in report["tests"])
    assert names == expected, report
    assert report["succeeded"] == len(expected) and report["failed"] == 0 and report["notRun"] == 0, report
    print(json.dumps(dict(ok=True, tests=sorted(names), returncode=process.returncode, report=str(output / "index.json"))))


if __name__ == "__main__":
    main()
