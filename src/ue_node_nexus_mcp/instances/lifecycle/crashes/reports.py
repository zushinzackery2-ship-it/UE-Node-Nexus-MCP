"""Bounded CrashContext reads tied to project, PID and process lifetime."""

from pathlib import Path
import re
import xml.etree.ElementTree as ET

from .details import log_details, stacks


def created_at(instance: dict) -> float:
    return int(instance["process_created"]) / 10_000_000 - 11_644_473_600


def read(path: Path, instance: dict, end: float) -> dict | None:
    info = path.stat()
    if not created_at(instance) - 2 <= info.st_mtime <= end + 120:
        return None
    if info.st_size > 2 * 1024 * 1024:
        raise ValueError("CrashContext exceeds the 2 MiB inspection budget")
    tree = ET.fromstring(path.read_bytes())
    values = tree.find("RuntimeProperties")
    if values is None or values.findtext("ProcessId") != str(instance["pid"]):
        return None
    command = values.findtext("CommandLine", "")
    identifier = re.search(r"-NexusInstance=([^\s]+)", command, re.IGNORECASE)
    if identifier and identifier[1] != instance["instance_id"]:
        return None
    game = values.findtext("GameName", "").casefold()
    project = Path(instance["project_path"]).stem.casefold()
    if game not in (project, "ue-" + project):
        return None
    ensure = values.findtext("IsEnsure", "false").lower() == "true" or values.findtext("CrashType") == "Ensure"
    result = dict(report_id=path.parent.name, crash_type=values.findtext("CrashType"), is_ensure=ensure,
                  error_message=values.findtext("ErrorMessage", "")[:8192], process_id=int(values.findtext("ProcessId")),
                  context_path=str(path), report_written_at=info.st_mtime,
                  is_oom=values.findtext("MemoryStats.bIsOOM", "0") == "1")
    result.update(stacks(values))
    dump = path.parent / "UEMinidump.dmp"
    if dump.is_file():
        result.update(dump_path=str(dump), dump_bytes=dump.stat().st_size)
    threads = []
    for thread in tree.findall(".//Thread"):
        if thread.findtext("IsCrashed") == "true" or thread.findtext("ThreadName") == "RenderThread":
            threads.append(dict(name=thread.findtext("ThreadName"), crashed=thread.findtext("IsCrashed") == "true",
                                call_stack=thread.findtext("CallStack", "")[:16384]))
    result["threads"] = threads[:4]
    logs = sorted(path.parent.glob("*.log"), key=lambda entry: entry.stat().st_mtime, reverse=True)
    if logs:
        result["log_path"] = str(logs[0])
        result.update(log_details(logs[0]))
    return result


def collect(instance: dict, end: float) -> dict:
    root = Path(instance["project_path"]).parent / "Saved/Crashes"
    found, errors = [], []
    if not root.is_dir():
        return dict(fatal=None, ensures=[], errors=[])
    directories = sorted((entry for entry in root.iterdir() if entry.is_dir()),
                         key=lambda entry: entry.stat().st_mtime, reverse=True)[:64]
    for directory in directories:
        path = directory / "CrashContext.runtime-xml"
        if not path.is_file():
            continue
        try:
            report = read(path, instance, end)
            if report:
                found.append(report)
        except (OSError, ValueError, ET.ParseError) as error:
            errors.append(dict(path=str(path), message=str(error)[:1024]))
    found.sort(key=lambda report: report["report_written_at"], reverse=True)
    return dict(fatal=next((report for report in found if not report["is_ensure"]), None),
                ensures=[report for report in found if report["is_ensure"]][:8], errors=errors[:8])
