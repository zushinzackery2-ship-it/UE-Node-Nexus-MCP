"""Run this round's native blueprint contract regressions in the isolated host."""

import json
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    host = root / "build/validation"
    engine = Path("D:/Program Files/Epic Games/UE_5.5")
    report = host / "Reports/BlueprintContracts-20261016"
    command = [
        str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(host / "NexusValidation.uproject"),
        "-nullrhi", "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
        "-stdout", "-FullStdOutLogOutput", "-ExecCmds=Automation RunTests Nexus.Blueprint",
        "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + str(report),
        "-NexusRuntime=" + str(host / "RuntimeBlueprintContracts"),
        "-abslog=" + str(host / "Logs/BlueprintContracts-20261016.log"),
    ]
    log = host / "Logs/BlueprintContracts-console-20261016.log"
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=900, check=True)
    results = json.loads((report / "index.json").read_text(encoding="utf-8-sig"))
    names = {test["fullTestPath"] for test in results["tests"]}
    counts = {key: results[key] for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess")}
    summary = dict(tests=sorted(names), returncode=result.returncode, counts=counts, report=str(report / "index.json"))
    print(json.dumps(summary), flush=True)
    expected = {"Nexus.Blueprint.StructGeneratedState", "Nexus.Blueprint.GetArrayItemAlias"}
    assert expected <= names, summary
    assert counts["failed"] == 0 and counts["notRun"] == 0 and counts["inProcess"] == 0, summary


if __name__ == "__main__":
    main()
