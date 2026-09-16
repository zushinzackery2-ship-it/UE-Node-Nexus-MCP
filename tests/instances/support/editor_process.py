"""Lightweight lifecycle peer for multi-process broker tests; never launches UE."""

import json
import os
from pathlib import Path
import sys
import threading

from ue_node_nexus_mcp.coordination.file_lock import FileLock
from ue_node_nexus_mcp.coordination.pipe_server import PipeServer
from ue_node_nexus_mcp.instances.broker.registry import atomic_json
from ue_node_nexus_mcp.instances.identity.processes import inspect_process


def run(root: Path, intent: Path):
    record = json.loads(intent.read_text(encoding="utf-8"))
    record.update(inspect_process(os.getpid()), ready=True, guard_protocol=1, contract_version=4,
                  context_epoch=1, rhi="d3d12", engine_dir="integration", launch_profile="offscreen")
    atomic_json(root / "Creations" / (str(os.getpid()) + ".json"), record)
    stop = threading.Event()
    grants = dict()
    lock = threading.Lock()

    def control(request, peer):
        action, payload = request["operation"], request.get("payload", dict())
        with lock:
            if action == "status":
                data = dict(record)
            elif action == "scope_grant":
                grants[payload["scope_id"]] = payload
                data = dict(granted=True)
            elif action == "scope_release":
                grants.pop(payload["scope_id"], None)
                data = dict(pending=0, inflight=0)
            elif action == "prepare_close":
                data = dict(blockers=["work_in_progress"] if grants else [], close_revision=1)
            elif action == "commit_close":
                assert not grants
                data = dict(exit_requested=True)
                stop.set()
            else:
                data = dict()
        return dict(ok=True, data=data)

    with FileLock(root / "ProjectLocks" / (record["project_key"] + ".lock")):
        server = PipeServer("\\\\.\\pipe\\UeNodeNexusLifecycle." + str(os.getpid()), control).start()
        path = root / "Instances" / (str(os.getpid()) + ".json")
        with FileLock(root / "fixture-observations.lock", timeout=5):
            atomic_json(path, record)
        try:
            stop.wait(300)
        finally:
            server.close()
            with FileLock(root / "fixture-observations.lock", timeout=5):
                path.unlink(missing_ok=True)


if __name__ == "__main__":
    run(Path(sys.argv[1]), Path(sys.argv[2]))
