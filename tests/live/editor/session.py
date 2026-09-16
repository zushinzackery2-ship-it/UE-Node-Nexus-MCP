"""Run bridge assertions in one explicitly selected, isolated editor."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import time

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.bridge import BridgeConfig, UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.session.binding import EditorSession as ManagedSession


class EditorSession:
    def __init__(self, project: Path, engine: Path, name: str, rhi: str = "d3d12") -> None:
        if rhi not in ("d3d12", "nullrhi"):
            raise ValueError("validation RHI must be d3d12 or nullrhi")
        self.project = project.resolve()
        self.project.relative_to((Path(__file__).resolve().parents[3] / "build").resolve())
        self.engine = engine.resolve()
        self.name = name
        self.rhi = rhi
        self.logs = self.project.parent / "Logs"
        self.logs.mkdir(parents=True, exist_ok=True)
        self.session = ManagedSession(BrokerClient(self.project.parent / "Runtime", str(self.project.parent)), str(self.project))
        self.previous_bridge = None
        self.requests = self.logs / f"{name}-requests.jsonl"

    def __enter__(self):
        self.previous_bridge = runtime.bridge
        runtime.bridge = UeBridgeClient(BridgeConfig(timeout_seconds=180), instances=self.session)
        try:
            result = self.session.ensure(dict(mode="reuse_or_start", engine_path=str(self.engine),
                                              launch_profile="offscreen", rhi=self.rhi, dry_run=False))
            self.report("launch", result)
            print(json.dumps(dict(phase="editor_start", instance_id=result["instance"]["instance_id"], project=str(self.project))), flush=True)
            self._ready()
        except BaseException:
            self.__exit__(True, None, None)
            raise
        return self

    def _ready(self) -> None:
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            status = self.session.status()
            if status["state"] in ("EXITED", "UNRESPONSIVE"):
                raise RuntimeError(f"validation editor unavailable: {status}")
            if status["state"] != "READY":
                time.sleep(0.5)
                continue
            response = self.call("project_context_get", dict())
            if response.get("ok") and response.get("data", dict()).get("project_name") == self.project.stem:
                self.report("context", response)
                return
            time.sleep(0.5)
        raise TimeoutError(f"editor did not become ready; see {self.logs}")

    def call(self, operation: str, payload: dict) -> dict:
        started = time.monotonic()
        response = runtime.call_bridge(operation, payload)
        with self.requests.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(dict(operation=operation, payload=payload, response=response,
                                         elapsed_seconds=time.monotonic() - started), ensure_ascii=False) + "\n")
        return response

    def require(self, operation: str, **payload) -> dict:
        response = self.call(operation, payload)
        if not response.get("ok"):
            raise AssertionError(json.dumps(response, ensure_ascii=False))
        return response.get("data") or dict()

    def report(self, stage: str, value) -> None:
        (self.logs / f"{self.name}-{stage}.json").write_text(
            json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def verify_builds(self) -> dict:
        capabilities = self.require("bridge_capabilities_get")
        for name in ("UeNodeNexusBridge", "UeNodeNexusGuard", "UeNodeNexusVfxBridge"):
            plugin = self.project.parent / "Plugins" / ("UeNodeNexusBridge" if name == "UeNodeNexusGuard" else name)
            expected = json.loads((plugin / "BuildIdentity.json").read_text(encoding="utf-8"))
            live = capabilities["build"][name]
            for field in ("version", "source_commit", "source_dirty", "source_fingerprint", "contract_version"):
                assert live[field] == expected[field], (name, field, live, expected)
            binary = plugin / f"Binaries/Win64/UnrealEditor-{name}.dll"
            assert Path(live["module_path"]).resolve() == binary.resolve(), live
            assert live["loaded"] and live["build_id"], live
            live["dll_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        self.report("builds", capabilities)
        return capabilities

    def __exit__(self, exc_type, _exception, _traceback):
        failure = None
        try:
            identifier = self.session.current().get("instance_id")
            if identifier:
                self.session.release()
                state = self.session.status()
                if state["state"] in ("READY", "IDLE", "BLOCKED"):
                    self.session.call("close", dict(instance_id=identifier, dry_run=False))
                    state = self.session.status()
                deadline = time.monotonic() + 75
                while state["state"] not in ("EXITED", "BLOCKED", "UNRESPONSIVE") and time.monotonic() < deadline:
                    time.sleep(0.5)
                    state = self.session.status()
                normal = state["state"] == "EXITED" and state.get("exit_code") == 0
                self.report("exit", dict(normal=normal, instance=state))
                if not normal:
                    failure = RuntimeError(f"validation editor did not exit normally: {state}")
        finally:
            if self.previous_bridge is not None:
                runtime.bridge = self.previous_bridge
            self.session.close()
        if failure is not None and not exc_type:
            raise failure
        return False
