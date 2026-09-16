"""Kill only an owned read-only soak's MCP processes and verify autonomous cleanup."""

import argparse
import ctypes
from ctypes import wintypes as w
import json
from pathlib import Path
import time

from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.identity.discovery import arguments, process_ids
from ue_node_nexus_mcp.instances.identity.paths import project_identity
from ue_node_nexus_mcp.instances.identity.processes import inspect_process, is_alive
from ue_node_nexus_mcp.instances.identity.windows import kernel
from .host import ROOT


def terminate_owned(identity):
    api = kernel()
    handle = api.OpenProcess(0x1000 | 0x100000 | 1, False, identity["pid"])
    if not handle:
        assert not is_alive(identity)
        return
    try:
        times = [w.FILETIME() for _ in range(4)]
        assert api.GetProcessTimes(handle, *[ctypes.byref(value) for value in times])
        created = str((times[0].dwHighDateTime << 32) | times[0].dwLowDateTime)
        assert created == identity["process_created"]
        api.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
        if not api.TerminateProcess(handle, 88) and api.WaitForSingleObject(handle, 0) != 0:
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        api.CloseHandle(handle)


def run(name, resume=False):
    host = (ROOT / "build" / name).resolve()
    host.relative_to((ROOT / "build").resolve())
    project = host / "NexusValidation.uproject"
    root = host / "Runtime"
    assert project.is_file() and (root / "manager.json").is_file()
    client = BrokerClient(root, str(host))
    report = dict(project=str(project), purpose="abrupt MCP death and last-client cleanup")
    try:
        rows = client.call("list", dict(project_path=str(project), include_exited=resume))["instances"]
        assert len(rows) == 1 and rows[0]["ownership"] == "managed" and not rows[0].get("dirty_packages"), rows
        instance = rows[0]
        report["instance_id"] = instance["instance_id"]
        owned = []
        for pid in process_ids(("python.exe", "pythonw.exe")):
            try:
                argv = arguments(pid)
                harness = "tests.instances.live.soak" in argv and name in argv
                server = "ue_node_nexus_mcp.server" in argv and "--project" in argv
                if server:
                    server = project_identity(argv[argv.index("--project") + 1])["project_key"] == instance["project_key"]
                identity = inspect_process(pid) if harness or server else None
                if identity:
                    owned.append(dict(identity, role="harness" if harness else "mcp"))
            except OSError:
                continue
        if resume:
            previous = json.loads((host / "client-crash-result.json").read_text())
            assert previous["instance_id"] == instance["instance_id"]
            owned = previous["terminated_clients"]
        assert sum(item["role"] == "mcp" for item in owned) >= 3, owned
        report["terminated_clients"] = owned
        for item in sorted(owned, key=lambda item: item["role"] != "harness"):
            if is_alive(item):
                terminate_owned(item)
        started = time.monotonic()
        while time.monotonic() - started < 210:
            state = client.call("status", dict(instance_id=instance["instance_id"]))
            assert state["state"] != "BLOCKED", state
            if state["state"] == "EXITED":
                break
            time.sleep(2)
        assert state["state"] == "EXITED" and state.get("exit_code") == 0, state
        assert state["use_count"] == state["scope_count"] == 0, state
        report.update(ok=True, final=state, reclaimed_seconds=time.monotonic() - started)
        print(json.dumps(dict(ok=True, reclaimed_seconds=report["reclaimed_seconds"], exit_code=0)), flush=True)
    finally:
        client.close()
        (host / "client-crash-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    run(args.name, args.resume)
