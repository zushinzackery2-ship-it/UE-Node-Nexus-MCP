"""Run bridge assertions in one explicitly selected, isolated editor."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from ue_node_nexus_mcp import runtime
from ue_node_nexus_mcp.bridge import BridgeConfig, BridgeError, UeBridgeClient
from ue_node_nexus_mcp.instance import instance_manager


def environment(project: Path) -> dict[str, str]:
    result = os.environ.copy()
    temporary = project.parent / "Temp"
    temporary.mkdir(parents=True, exist_ok=True)
    result.update(TEMP=str(temporary), TMP=str(temporary))
    result["UE-LocalDataCachePath"] = str(project.parent / "DerivedDataCache")
    return result


class EditorSession:
    def __init__(self, project: Path, engine: Path, name: str, rhi: str = "d3d12") -> None:
        if rhi not in ("d3d12", "nullrhi"):
            raise ValueError("validation RHI must be d3d12 or nullrhi")
        self.project = project.resolve()
        self.engine = engine.resolve()
        self.name = name
        self.rhi = rhi
        self.logs = self.project.parent / "Logs"
        self.logs.mkdir(parents=True, exist_ok=True)
        self.process = None
        self.output = None
        self.previous_bridge = None
        self.requests = self.logs / f"{name}-requests.jsonl"

    def __enter__(self):
        command = [
            str(self.engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(self.project),
            "-" + self.rhi, "-unattended", "-nosplash", "-nosound", "-NoSourceControl", "-nop4",
            "-stdout", "-FullStdOutLogOutput", "-RenderOffscreen", "-NoVSync",
            "-abslog=" + str(self.logs / f"{self.name}-editor.log"),
        ]
        self.output = (self.logs / f"{self.name}-console.log").open("w", encoding="utf-8")
        self.process = subprocess.Popen(command, env=environment(self.project), stdout=self.output,
                                        stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        self.previous_bridge = runtime.bridge
        runtime.bridge = UeBridgeClient(BridgeConfig(timeout_seconds=180))
        self.report("launch", dict(command=command, pid=self.process.pid))
        print(json.dumps(dict(phase="editor_start", pid=self.process.pid, project=str(self.project), rhi=self.rhi)), flush=True)
        try:
            self._ready()
        except BaseException:
            self.__exit__(True, None, None)
            raise
        return self

    def _ready(self) -> None:
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"editor exited with {self.process.returncode}; see {self.logs}")
            try:
                instance_manager.select(pid=self.process.pid)
            except BridgeError:
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
        for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge"):
            plugin = self.project.parent / "Plugins" / name
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
            if self.process is not None and self.process.poll() is None:
                self.call("editor_request_exit", dict(save_before_exit=False, force=False))
                try:
                    self.process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    self.process.wait(timeout=30)
                    failure = TimeoutError(f"validation editor did not exit normally: {self.name}")
                if self.process.returncode != 0:
                    failure = RuntimeError(f"validation editor exited with {self.process.returncode}: {self.name}")
                self.report("exit", dict(returncode=self.process.returncode, normal=failure is None))
        finally:
            if self.previous_bridge is not None:
                runtime.bridge = self.previous_bridge
            if self.output is not None:
                self.output.close()
        if failure is not None and not exc_type:
            raise failure
        return False
