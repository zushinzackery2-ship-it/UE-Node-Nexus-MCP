"""Run bridge assertions in one explicitly selected, isolated editor."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import time

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.bridge import BridgeConfig, UeBridgeClient
from ue_node_nexus_mcp.instances.broker.client import BrokerClient
from ue_node_nexus_mcp.instances.errors import InstanceError
from ue_node_nexus_mcp.instances.session.binding import EditorSession as ManagedSession


# A cold validation editor compiles shaders before it is ready; ensure holds for it.
STARTUP_SECONDS = 600


class EditorSession:
    def __init__(self, project: Path, engine: Path, name: str, rhi: str = "d3d12", allow_low_memory: bool = False) -> None:
        if rhi not in ("d3d12", "d3d11"):
            raise ValueError("visible validation RHI must be d3d12 or d3d11")
        self.project = project.resolve()
        self.project.relative_to((Path(__file__).resolve().parents[3] / "build").resolve())
        self.engine = engine.resolve()
        self.name = name
        self.rhi = rhi
        self.allow_low_memory = allow_low_memory
        self.logs = self.project.parent / "Logs"
        self.logs.mkdir(parents=True, exist_ok=True)
        self.session = ManagedSession(BrokerClient(self.project.parent / "Runtime", str(self.project.parent)), str(self.project))
        self.startup = None
        self.previous_bridge = None
        self.requests = self.logs / f"{name}-requests.jsonl"
        self.saved_packages = set()

    def __enter__(self):
        self.previous_bridge = runtime.bridge
        runtime.bridge = UeBridgeClient(BridgeConfig(timeout_seconds=180), instances=self.session)
        try:
            result = self.session.ensure(dict(mode="reuse_or_start", engine_path=str(self.engine), launch_profile="interactive",
                                              rhi=self.rhi, dry_run=False, wait_seconds=STARTUP_SECONDS,
                                              allow_low_memory=self.allow_low_memory))
            self.report("launch", result)
            print(json.dumps(dict(phase="editor_start", instance_id=result["instance"]["instance_id"], project=str(self.project))), flush=True)
            self._ready(result["startup"])
        except BaseException:
            self.__exit__(True, None, None)
            raise
        return self

    def _ready(self, startup: dict) -> None:
        """Ensure returns once the start ended; a managed editor answers its own advisories."""
        self.startup = startup
        self.report("startup", startup)
        if startup["outcome"] != "ready":
            raise RuntimeError(f"validation editor did not become ready: {startup}")
        response = self.call("project_context_get", dict())
        if not response.get("ok") or response.get("data", dict()).get("project_name") != self.project.stem:
            raise RuntimeError(f"validation editor answers for another project: {response}")
        self.report("context", response)

    def call(self, operation: str, payload: dict) -> dict:
        started = time.monotonic()
        response = runtime.call_bridge(operation, payload)
        if operation == "asset_save" and response.get("ok"):
            dirty = response.get("data", dict()).get("dirty_state", dict())
            if dirty.get("package_dirty") is False:
                self.saved_packages.add(dirty["package_name"])
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

    def verify_builds(self, plugin_root: Path | None = None) -> dict:
        capabilities = self.require("bridge_capabilities_get")
        plugins = plugin_root if plugin_root is not None else self.project.parent / "Plugins"
        for name in ("UeNodeNexusBridge", "UeNodeNexusGuard", "UeNodeNexusVfxBridge"):
            plugin = plugins / ("UeNodeNexusBridge" if name == "UeNodeNexusGuard" else name)
            expected = json.loads((plugin / "BuildIdentity.json").read_text(encoding="utf-8"))
            live = capabilities["build"][name]
            for field in ("version", "source_commit", "source_dirty", "source_fingerprint", "contract_version"):
                assert live[field] == expected[field], (name, field, live, expected)
            binary = plugin / f"Binaries/Win64/UnrealEditor-{name}.dll"
            # UE module filenames are absolute or relative to FPlatformProcess::BaseDir().
            loaded_path = (self.engine / "Engine/Binaries/Win64" / live["module_path"]).resolve()
            assert loaded_path == binary.resolve(), live
            assert live["loaded"] and live["build_id"], live
            live["module_path_resolved"] = str(loaded_path)
            live["dll_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        self.report("builds", capabilities)
        return capabilities

    def __exit__(self, exc_type, _exception, _traceback):
        failure, normal = None, False
        state = self.session.current()
        try:
            identifier = state.get("instance_id")
            if identifier:
                self.session.release()
                state = self.session.status()
                if state["state"] in ("READY", "IDLE", "BLOCKED"):
                    from .close import close_owned

                    result = close_owned(self.session, identifier, self.saved_packages)
                    state = result["instance"]
                deadline = time.monotonic() + 75
                while state["state"] not in ("EXITED", "BLOCKED", "UNRESPONSIVE") and time.monotonic() < deadline:
                    time.sleep(0.5)
                    state = self.session.status()
                normal = state["state"] == "EXITED" and state.get("exit_code") == 0
                if not normal:
                    failure = RuntimeError(f"validation editor did not exit normally: {state}")
            else:
                normal = True
        except Exception as error:
            failure = error
            if isinstance(error, InstanceError):
                state = error.details.get("instance", state)
        finally:
            if self.previous_bridge is not None:
                runtime.bridge = self.previous_bridge
            try:
                self.session.close()
            except Exception as error:
                failure = failure or error
            detail = failure.envelope()["error"] if isinstance(failure, InstanceError) else (
                dict(code=type(failure).__name__, message=str(failure)) if failure else None)
            self.report("exit", dict(normal=normal and failure is None, instance=state, error=detail))
        if failure is not None and exc_type:
            print(f"Validation cleanup also failed: {failure}", flush=True)
        if failure is not None and not exc_type:
            raise failure
        return False
