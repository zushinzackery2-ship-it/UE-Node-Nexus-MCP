"""Execute UE Automation with a real RHI and a reproducible sequence seed."""

import json
from pathlib import Path
import subprocess
import time
import re


def run(project, engine, directory, seed, steps, full=False, rhi="d3d12", oracle=None):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    report = directory / "Automation"
    command = [str(engine / "Engine/Binaries/Win64/UnrealEditor.exe"), str(project),
               "-" + rhi, "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
               "-windowed", "-ResX=960", "-ResY=540", "-stdout", "-FullStdOutLogOutput",
               "-ExecCmds=Automation RunTests " + ("Nexus.Stress.ColdReadback" if oracle else "Nexus." if full else "Nexus.Stress."),
               "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + str(report),
               "-NexusStressSeed=" + str(seed), "-NexusStressSteps=" + str(steps),
               "-NexusRuntime=" + str(project.parent / "RuntimeAutomation"),
               "-abslog=" + str(directory / "Editor.log")]
    if oracle:
        command.append("-NexusStressOracle=" + str(oracle))
    (directory / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    start = time.monotonic()
    result = dict(ok=False, seed=seed, steps=steps, rhi=rhi, report=str(report / "index.json"))
    try:
        with (directory / "Console.log").open("w", encoding="utf-8") as stream:
            completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=900)
        result["exit_code"] = completed.returncode
    except BaseException as error:
        result["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        result["seconds"] = time.monotonic() - start
        (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    payload = json.loads((report / "index.json").read_text(encoding="utf-8-sig")) if (report / "index.json").is_file() else {}
    counts = {name: payload.get(name) for name in ("succeeded", "succeededWithWarnings", "failed", "notRun", "inProcess")}
    expected = "Nexus.Stress.ColdReadback" if oracle else "Nexus.Stress.Blueprint.CallerSignatureSequence"
    matching = [test for test in payload.get("tests", []) if test.get("fullTestPath", "").startswith(expected)]
    passed = completed.returncode == 0 and bool(matching) and bool(counts["succeeded"]) and all(
        counts[name] == 0 for name in counts if name != "succeeded")
    log = (directory / "Editor.log").read_text(encoding="utf-8-sig", errors="replace")
    loaded = dict(re.findall(r"module_identity module=(\w+).*?fingerprint=([0-9a-f]+)", log))
    plugins = project.parent / "Plugins" if (project.parent / "Plugins/UeNodeNexusBridge").exists() else engine / "Engine/Plugins/Marketplace"
    for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge"):
        plugin = "UeNodeNexusBridge" if name == "UeNodeNexusGuard" else name
        identity = json.loads((plugins / plugin / "BuildIdentity.json").read_text(encoding="utf-8-sig"))
        passed = passed and loaded.get(name) == identity["source_fingerprint"]
    failures = [dict(code="native_assertion", test=test.get("fullTestPath"),
                     messages=[entry["event"]["message"] for entry in test.get("entries", [])
                               if entry.get("event", {}).get("type") == "Error"])
                for test in payload.get("tests", []) if test.get("state") == "Fail"]
    result.update(ok=passed, counts=counts, expected_tests=expected, matched_tests=len(matching), build=loaded, failures=failures)
    (directory / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if not passed:
        raise AssertionError(json.dumps(result))
    return result
