"""Real OS child processes around the production Broker with a lightweight editor."""

import json
import os
from pathlib import Path
import subprocess
import sys
import ue_node_nexus_mcp

from ue_node_nexus_mcp.coordination.file_lock import FileLock
from ue_node_nexus_mcp.instances.broker.registry import atomic_json
from ue_node_nexus_mcp.instances.identity.processes import inspect_process
from ue_node_nexus_mcp.instances.lifecycle.platform import WindowsPlatform

ROOT = Path(__file__).resolve().parents[3]


def fault(root, stage):
    path = root / "fault.json"
    record = json.loads(path.read_text()) if path.is_file() else dict()
    if record.get("stage") == stage and not record.get("fired"):
        atomic_json(path, dict(record, fired=True))
        os._exit(81)


def environment() -> dict:
    package_root = Path(ue_node_nexus_mcp.__file__).resolve().parent.parent
    return dict(os.environ, PYTHONPATH=os.pathsep.join((str(package_root), str(ROOT))))


class ProcessPlatform(WindowsPlatform):
    def __init__(self, root):
        super().__init__(root)
        self.initial_scan = True

    def launch_options(self, project, payload):
        return dict(executable=sys._base_executable, engine_dir="integration", rhi="d3d12", launch_profile="offscreen")

    def compatible(self, instance, payload):
        return None

    def discover(self, project_key=None):
        for identifier, process in list(self.processes.items()):
            if process.poll() is not None:
                self.exit_codes[identifier] = process.returncode
                self.processes.pop(identifier)
        result = []
        with FileLock(self.root / "fixture-observations.lock", timeout=5):
            for file in (self.root / "Instances").glob("*.json"):
                record = json.loads(file.read_text(encoding="utf-8"))
                if self.alive(record) and (not project_key or record["project_key"] == project_key):
                    result.append(record)
        if self.initial_scan:
            self.initial_scan = False
            from ue_node_nexus_mcp.instances.identity.discovery import arguments, process_ids
            known = set(row["pid"] for row in result)
            for pid in process_ids(("python.exe",)):
                try:
                    argv = arguments(pid)
                    if pid in known or "tests.instances.support.editor_process" not in argv:
                        continue
                    if Path(argv[-2]).resolve() != self.root.resolve():
                        continue
                    record = json.loads(Path(argv[-1]).read_text())
                    record.update(inspect_process(pid), ready=False)
                    result.append(record)
                except (OSError, ValueError):
                    continue
        return result

    def launch(self, instance):
        intent = self.root / "Launches" / (instance["instance_id"] + ".json")
        atomic_json(intent, instance)
        fault(self.root, "before_spawn")
        process = subprocess.Popen([sys._base_executable, "-m", "tests.instances.support.editor_process", str(self.root), str(intent)],
            cwd=str(self.root), env=environment(), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
        fault(self.root, "after_spawn")
        self.processes[instance["instance_id"]] = process
        identity = inspect_process(process.pid)
        if not identity:
            raise RuntimeError("integration editor exited during launch")
        return dict(identity, engine_dir="integration", rhi="d3d12", launch_profile="offscreen")


def start_broker(root: Path):
    root.mkdir(parents=True, exist_ok=True)
    atomic_json(root / "policy.json", dict(sweep_seconds=0.05, grace_seconds=1,
                recovery_seconds=0.2, broker_idle_seconds=0.5, min_free_gib=0.01, min_free_ratio=0.001))
    output = (root / "fixture-stderr.log").open("ab")
    try:
        return subprocess.Popen([sys._base_executable, "-m", "tests.instances.support.processes", str(root)],
            cwd=str(root), env=environment(), stdin=subprocess.DEVNULL, stdout=output, stderr=output,
            creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        output.close()


if __name__ == "__main__":
    from ue_node_nexus_mcp.instances.broker.main import serve
    from ue_node_nexus_mcp.instances.broker.registry import Registry
    runtime = Path(sys.argv[1])
    put = Registry.put

    def persist(registry, category, identifier, record):
        starting = category == "instance" and record.get("state") == "STARTING"
        if starting and not record.get("pid"):
            fault(runtime, "before_intent")
        put(registry, category, identifier, record)
        if starting:
            fault(runtime, "after_pid" if record.get("pid") else "after_intent")

    Registry.put = persist
    serve(runtime, ProcessPlatform)
