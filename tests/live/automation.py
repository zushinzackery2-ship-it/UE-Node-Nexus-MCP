"""Run every Nexus native regression in the isolated compiled host."""

import json
import os
from pathlib import Path
import subprocess


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    host = root / "build/validation"
    engine = Path(os.environ.get("UE_NEXUS_ENGINE_DIR", "D:/Program Files/Epic Games/UE_5.5"))
    report = host / "Reports/Issues4Final"
    command = [
        str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(host / "NexusValidation.uproject"),
        "-nullrhi", "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
        "-stdout", "-FullStdOutLogOutput", "-ExecCmds=Automation RunTests Nexus.",
        "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + str(report),
        "-NexusRuntime=" + str(host / "RuntimeAutomation"),
        "-abslog=" + str(host / "Logs/Issues4Final.log"),
    ]
    with (host / "Logs/Issues4Final-console.log").open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                       creationflags=subprocess.CREATE_NO_WINDOW, timeout=300, check=True)
    results = json.loads((report / "index.json").read_text(encoding="utf-8-sig"))
    counts = dict((key, results[key]) for key in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess"))
    assert counts["succeeded"] >= 31 and all(counts[key] == 0 for key in counts if key != "succeeded"), counts
    assert counts["succeeded"] == len(results["tests"]), counts
    print(json.dumps(dict(ok=True, report=str(report / "index.json"), **counts)))


if __name__ == "__main__":
    main()
