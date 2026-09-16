"""OS processes and native lifecycle IPC behind one injectable boundary."""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor

from ...errors import BridgeError
from ...transport import named_pipe_transport
from ..errors import InstanceError
from ..identity.paths import project_identity
from ..identity.processes import inspect_process, is_alive

LOG = logging.getLogger(__name__)


class WindowsPlatform:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.processes = dict()
        self.discovery_errors = []
        self.probes = ThreadPoolExecutor(max_workers=4, thread_name_prefix="nexus-observe")
        self.scan_lock = threading.Lock()
        self.scan_at = 0.0
        self.catalog = []
        self.resources = dict()
        self.resource_seconds = 30
        self.watches = dict()
        self.exit_codes = dict()

    def inspect(self, pid: int) -> dict | None:
        return inspect_process(pid)

    def alive(self, identity: dict) -> bool:
        return is_alive(identity)

    def memory(self) -> dict:
        from ..identity.discovery import memory_status
        return memory_status()

    def launch_options(self, project: dict, payload: dict) -> dict:
        from .engine import launch_options
        return launch_options(project, payload)

    def compatible(self, instance: dict, payload: dict) -> None:
        from .engine import compatible
        compatible(instance, payload)

    def control(self, instance: dict, action: str, payload: dict) -> dict:
        try:
            response = named_pipe_transport.send("\\\\.\\pipe\\UeNodeNexusLifecycle." + str(instance["pid"]),
                                                 dict(operation=action, request_id=uuid.uuid4().hex, payload=payload), 3)
        except (BridgeError, OSError) as exc:
            raise InstanceError("instance_unresponsive", str(exc), dict(instance_id=instance["instance_id"])) from exc
        if not response.get("ok"):
            error = response.get("error", dict())
            raise InstanceError(error.get("code", "lifecycle_error"), error.get("message", "lifecycle operation failed"), error.get("details"))
        return response["data"]

    def discover(self, project_key: str | None = None) -> list[dict]:
        with self.scan_lock:
            if time.monotonic() - self.scan_at >= 1:
                self.catalog = self._discover()
                self.scan_at = time.monotonic()
            return [dict(item) for item in self.catalog if not project_key or item["project_key"] == project_key]

    def _probe(self, record: dict) -> dict:
        try:
            observed = self.control(record, "status", dict())
            if any(observed.get(key) != record.get(key) for key in ("pid", "process_created", "project_key")):
                raise InstanceError("instance_unverified", "lifecycle identity differs from the observed process")
            record.update(observed, control_available=True)
        except InstanceError as exc:
            record.update(ready=False, control_available=False, control_error=exc.envelope()["error"])
        from ..identity.resources import sample
        cached = self.resources.get(record["instance_id"], dict())
        if time.monotonic() - cached.get("at", 0) >= self.resource_seconds:
            try:
                cached = dict(at=time.monotonic(), data=sample(record))
                self.resources[record["instance_id"]] = cached
            except OSError:
                pass
        record.update(cached.get("data", dict()))
        return record

    def _discover(self) -> list[dict]:
        from ..identity.discovery import arguments, commandlet, editor_pids
        result, seen = [], set()
        self.discovery_errors = []
        for identifier, watch in list(self.watches.items()):
            code = watch.poll()
            if code is not None:
                self.exit_codes[identifier] = code
                watch.close()
                self.watches.pop(identifier)
        for path in (self.root / "Instances").glob("*.json"):
            try:
                record = json.loads(path.read_text(encoding="utf-8-sig"))
                if not self.alive(record):
                    path.unlink(missing_ok=True)
                    continue
                seen.add(record["pid"])
                result.append(record)
            except (OSError, KeyError, ValueError, InstanceError) as exc:
                self.discovery_errors.append(dict(record=str(path), message=str(exc)))
                LOG.warning("unreadable lifecycle record: %s", path)
        for pid in editor_pids():
            if pid in seen:
                continue
            try:
                identity = self.inspect(pid)
                if not identity:
                    continue
                argv = arguments(pid)
                if commandlet(argv):
                    continue
                project = next((value for value in argv if value.lower().endswith(".uproject")), None)
                if not project:
                    continue
                info = project_identity(project)
                instance_id = next((value.split("=", 1)[1] for value in argv if value.startswith("-NexusInstance=")),
                                   f"external-{pid}-{identity['process_created']}")
                item = dict(identity, **info, instance_id=instance_id, ready=False, guard_protocol=0,
                            launch_profile="offscreen" if "-RenderOffscreen" in argv else "interactive")
                result.append(item)
            except (OSError, InstanceError) as exc:
                self.discovery_errors.append(dict(pid=pid, message=str(exc)))
        records = list(self.probes.map(self._probe, result))
        from ..identity.watch import ProcessWatch
        for record in records:
            if record["instance_id"] not in self.watches:
                try:
                    self.watches[record["instance_id"]] = ProcessWatch(record)
                except OSError:
                    continue
        live = set(item["instance_id"] for item in records)
        self.resources = dict((key, value) for key, value in self.resources.items() if key in live)
        for identifier, process in list(self.processes.items()):
            if process.poll() is not None:
                self.exit_codes[identifier] = process.returncode
                self.processes.pop(identifier)
        while len(self.exit_codes) > 256:
            self.exit_codes.pop(next(iter(self.exit_codes)))
        return records

    def exit_result(self, instance: dict) -> dict:
        identifier = instance["instance_id"]
        with self.scan_lock:
            watch = self.watches.get(identifier)
            if watch is not None:
                code = watch.poll()
                if code is not None:
                    self.exit_codes[identifier] = code
                    self.watches.pop(identifier).close()
            process = self.processes.get(identifier)
            if process is not None and process.poll() is not None:
                self.exit_codes[identifier] = process.returncode
                self.processes.pop(identifier)
            return dict(exit_code=self.exit_codes.get(identifier))

    def launch(self, instance: dict) -> dict:
        if self.discovery_errors:
            raise InstanceError("instance_unverified", "existing editor identities could not be verified", dict(items=self.discovery_errors))
        options = instance["launch"]
        logs = Path(instance["project_path"]).parent / "Saved/Nexus/Logs"
        logs.mkdir(parents=True, exist_ok=True)
        command = [options["executable"], instance["project_path"], "-" + options["rhi"], "-nosplash",
                   "-NexusInstance=" + instance["instance_id"], "-NexusIntent=" + instance["start_intent_id"],
                   "-NexusRuntime=" + str(self.root), "-abslog=" + str(logs / (instance["instance_id"] + ".log"))]
        if options["launch_profile"] == "offscreen":
            command.extend(["-unattended", "-RenderOffscreen", "-nosound", "-NoVSync", "-NoSourceControl", "-nop4"])
        with (logs / (instance["instance_id"] + "-console.log")).open("ab") as output:
            process = subprocess.Popen(command, cwd=str(Path(instance["project_path"]).parent),
                                       stdout=output, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
        self.processes[instance["instance_id"]] = process
        try:
            identity = self.inspect(process.pid)
        except (OSError, InstanceError) as exc:
            raise InstanceError("launch_outcome_unknown", "created editor requires identity reconciliation",
                                dict(spawned_pid=process.pid)) from exc
        if identity is None:
            raise InstanceError("launch_failed", "editor exited during startup", dict(returncode=process.poll()))
        return dict(identity, engine_dir=options["engine_dir"], launch_profile=options["launch_profile"], rhi=options["rhi"])

    def close(self) -> None:
        self.probes.shutdown(wait=True, cancel_futures=True)
        for process in self.processes.values():
            process.poll()
        for watch in self.watches.values():
            watch.close()
        self.watches.clear()
