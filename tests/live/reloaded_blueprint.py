"""Exercise native checkpoint reload retirement in the compiled Nexus host."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path("D:/Program Files/Epic Games/UE_5.5")


def main():
    host = ROOT / "build/validation"
    output = host / "Reports/ReloadedBlueprint-20261015"
    command = [
        str(ENGINE / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(host / "NexusValidation.uproject"),
        "-nullrhi", "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
        "-stdout", "-FullStdOutLogOutput", "-ExecCmds=Automation RunTests Nexus.IssuesNew.Reloaded",
        "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + str(output),
        "-NexusRuntime=" + str(host / "RuntimeReloadedBlueprint"),
        "-abslog=" + str(host / "Logs/ReloadedBlueprint-20261015.log"),
    ]
    log = host / "Logs/ReloadedBlueprint-console-20261015.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=300, check=True)
    report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
    expected = {"Nexus.IssuesNew.ReloadedBlueprintExcludedFromPIE",
                "Nexus.IssuesNew.ReloadedFunctionLibraryExcludedFromPIE"}
    names = {test["fullTestPath"] for test in report["tests"]}
    counts = {key: report[key] for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess")}
    summary = dict(tests=sorted(names), returncode=result.returncode, counts=counts,
                   report=str(output / "index.json"))
    print(json.dumps(summary), flush=True)
    assert names == expected and counts["succeeded"] == 2, summary
    assert all(counts[key] == 0 for key in counts if key != "succeeded"), summary


if __name__ == "__main__":
    main()
