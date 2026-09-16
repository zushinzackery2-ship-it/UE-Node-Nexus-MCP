from __future__ import annotations

import threading

from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.identity.processes import inspect_process, is_alive


class Clock:
    def __init__(self):
        self.value = 1000.0

    def __call__(self):
        return self.value

    def advance(self, seconds):
        self.value += seconds


class Platform:
    def __init__(self):
        self.records = dict()
        self.grants = dict()
        self.spawn_count = 0
        self.closed = []
        self.blockers = []
        self.busy = False
        self.lock = threading.Lock()

    def inspect(self, pid):
        for record in self.records.values():
            if record["pid"] == pid:
                return dict((key, record[key]) for key in ("pid", "process_created", "executable"))
        return inspect_process(pid)

    def alive(self, identity):
        if identity["pid"] >= 100000000:
            return any(record["pid"] == identity["pid"] and record["process_created"] == identity["process_created"]
                       for record in self.records.values())
        return is_alive(identity)

    def memory(self):
        return dict(total=64 * 1024 ** 3, available=48 * 1024 ** 3)

    def launch_options(self, project, payload):
        return dict(engine_dir="engine", executable="editor", launch_profile=payload.get("launch_profile", "offscreen"), rhi="d3d12")

    def compatible(self, instance, payload):
        if payload.get("rhi") and instance["rhi"] != payload["rhi"]:
            raise InstanceError("instance_incompatible", "wrong RHI")

    def launch(self, instance):
        with self.lock:
            self.spawn_count += 1
            identity = dict(pid=100000000 + self.spawn_count, process_created=str(self.spawn_count), executable="editor")
            record = dict(instance, **identity)
            record.update(ready=True, guard_protocol=1, contract_version=4, engine_dir="engine", rhi="d3d12")
            self.records[instance["instance_id"]] = record
            return identity

    def discover(self, key=None):
        with self.lock:
            return [dict(item) for item in self.records.values() if key is None or item["project_key"] == key]

    def control(self, instance, action, payload):
        if action == "scope_grant":
            self.grants[payload["scope_id"]] = payload
            return dict(granted=True)
        if action == "scope_release":
            if self.busy:
                return dict(pending=0, inflight=1)
            self.grants.pop(payload["scope_id"], None)
            return dict(pending=0, inflight=0)
        if action == "prepare_close":
            return dict(blockers=self.blockers, close_revision="1")
        if action == "commit_close":
            self.closed.append(instance["instance_id"])
            self.records.pop(instance["instance_id"])
        return dict(ok=True)
