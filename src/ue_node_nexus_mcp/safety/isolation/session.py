"""Own one private editor without rebinding the caller's active session."""

from __future__ import annotations

import json
from pathlib import Path
import time

from ...bridge import BridgeConfig, UeBridgeClient
from ...instances.broker.client import BrokerClient
from ...instances.errors import InstanceError, require
from ...instances.session.binding import EditorSession
from ...instances.session.shutdown import close_instance
from ..capture.status import status as capture_status


def expected_builds(project: Path, engine: Path) -> dict:
    result = dict()
    for name in ("UeNodeNexusBridge", "UeNodeNexusVfxBridge"):
        local = list((project.parent / "Plugins").rglob(name + ".uplugin")) if (project.parent / "Plugins").is_dir() else []
        if local:
            require(len(local) == 1, "isolation_identity_mismatch", "duplicate project Nexus plugins", plugin=name)
            directory = local[0].parent
        else:
            directory = engine / "Engine/Plugins/Marketplace" / name
        metadata = directory / "BuildIdentity.json"
        require(metadata.is_file(), "isolation_identity_missing", "validation requires recorded plugin identities", plugin=name)
        result[name] = dict(json.loads(metadata.read_text(encoding="utf-8-sig")),
                            binary=str(directory / "Binaries/Win64" / ("UnrealEditor-" + name + ".dll")))
    result["UeNodeNexusGuard"] = dict(result["UeNodeNexusBridge"], binary=str(
        Path(result["UeNodeNexusBridge"]["binary"]).with_name("UnrealEditor-UeNodeNexusGuard.dll")))
    return result


class ValidationSession:
    def __init__(self, project: Path, engine: Path, root: Path, rhi: str, timeout: float, allow_low_memory: bool = False):
        self.project, self.engine, self.rhi, self.timeout = project, engine, rhi, timeout
        self.allow_low_memory = allow_low_memory
        self.manager = EditorSession(BrokerClient(root / "Runtime", str(project.parent)), str(project))
        self.bridge = UeBridgeClient(BridgeConfig(timeout_seconds=timeout), instances=self.manager)
        self.expected = expected_builds(project, engine)
        self.requests = root / "requests.jsonl"

    def call(self, operation: str, payload: dict) -> dict:
        from ...runtime import call_bridge
        response = call_bridge(operation, payload, client=self.bridge)
        with self.requests.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(dict(operation=operation, payload=payload, response=response), ensure_ascii=False) + "\n")
        return response

    def start(self) -> dict:
        launch = self.manager.ensure(dict(project_path=str(self.project), engine_path=str(self.engine),
                                          mode="reuse_or_start", rhi=self.rhi, dry_run=False, wait_seconds=self.timeout,
                                          allow_low_memory=self.allow_low_memory))
        require(launch["startup"]["outcome"] == "ready", "isolation_startup_failed", "validation editor did not become ready", startup=launch["startup"])
        context = self.call("project_context_get", dict())
        require(context.get("ok") and Path(context["data"]["project_file_path"]).resolve() == self.project.resolve(),
                "isolation_wrong_project", "worker answered for a different project")
        capabilities = self.call("bridge_capabilities_get", dict())
        require(capabilities.get("ok"), "isolation_identity_mismatch", "worker capabilities were unavailable", response=capabilities)
        builds = capabilities["data"]["build"]
        require(len(set(builds.get(name, dict()).get("build_id") for name in self.expected)) == 1,
                "isolation_identity_mismatch", "worker module BuildIds differ")
        for name, expected in self.expected.items():
            live = builds.get(name, dict())
            loaded = (self.engine / "Engine/Binaries/Win64" / live.get("module_path", "")).resolve()
            require(live.get("loaded") and live.get("build_id") and loaded == Path(expected["binary"]).resolve(),
                    "isolation_identity_mismatch", "worker loaded an unexpected module binary", plugin=name)
            for key in ("source_fingerprint", "source_commit", "version", "contract_version", "source_dirty"):
                require(live.get(key) == expected.get(key), "isolation_identity_mismatch",
                        "worker plugin differs from the copied input", plugin=name, field=key)
        measured_rhi = capabilities["data"].get("rhi", "").lower()
        require(measured_rhi == self.rhi, "isolation_identity_mismatch",
                "worker RHI differs from the requested renderer", requested=self.rhi, actual=measured_rhi)
        return dict(instance=launch["instance"], build=builds, rhi=measured_rhi,
                    environment=capabilities["data"].get("safety_environment"))

    def execute(self, item: dict) -> dict:
        if item["operation"] in ("transcode_apply", "vfx_transcode_apply"):
            from .publication import execute
            return execute(self, item)
        response = self.call(item["operation"], item["payload"])
        if response.get("ok") and item["operation"] == "runtime_smoke_start":
            from ..runtime.result import wait_for_result
            return wait_for_result(self, response)
        if response.get("ok") and item["operation"] == "viewport_capture":
            capture = response["data"]
            require(capture.get("capture_protocol") == 1, "isolation_identity_mismatch", "worker lacks guarded capture protocol")
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                result = capture_status(capture["file_path"], capture["capture_id"])
                if result["data"]["done"]:
                    return result
                time.sleep(0.1)
            require(False, "isolation_capture_timeout", "capture did not produce a terminal receipt")
        return response

    def close(self) -> dict:
        state = self.manager.current()
        try:
            if not state.get("instance_id"):
                return dict(exit_confirmed=True, launched=False)
            self.manager.release()
            state = self.manager.status(dict(fresh=True))
            if state["state"] == "EXITED":
                return dict(exit_confirmed=True, exit_code=state.get("exit_code"), instance=state)
            for package in state.get("dirty_packages", []):
                require(package.startswith("/Game/"), "isolation_shared_package_dirty",
                        "a shared engine/plugin package became dirty; it will not be saved", package=package)
                saved = self.call("asset_save", dict(asset_path=package))
                require(saved.get("ok"), "isolation_cleanup_failed", "could not save the copied package before closing", response=saved)
            self.manager.release()
            deadline = time.monotonic() + max(90, self.timeout)
            while True:
                preview = close_instance(self.manager, dict(instance_id=state["instance_id"], dry_run=True))
                blockers = set(preview.get("blockers", []))
                if "instance_dirty" in blockers:
                    for package in preview.get("instance", dict()).get("dirty_packages", []):
                        require(package.startswith("/Game/"), "isolation_shared_package_dirty",
                                "a shared package will not be saved during validation cleanup", package=package)
                        saved = self.call("asset_save", dict(asset_path=package))
                        require(saved.get("ok"), "isolation_cleanup_failed", "could not save the copied package", response=saved)
                    self.manager.release()
                    require(time.monotonic() < deadline, "isolation_cleanup_failed", "copied packages remained dirty")
                    time.sleep(0.25)
                    continue
                users = preview.get("instance", dict()).get("users", [])
                if "instance_in_use" in blockers and all(user.get("client_session_id") == self.manager.broker.session_id for user in users):
                    blockers.remove("instance_in_use")
                if not blockers:
                    break
                require(blockers <= set(("recovery_quarantine", "work_in_progress", "editor_busy"))
                        and time.monotonic() < deadline, "isolation_cleanup_failed",
                        "validation copy could not finish its close prerequisites", blockers=sorted(blockers), instance=preview.get("instance"))
                time.sleep(0.25)
            return close_instance(self.manager, dict(instance_id=state["instance_id"], dry_run=False))
        except Exception as error:
            # Failed/stalled validation is cancelled, never resumed in uncertain state.
            # The copied project, inputs, receipts and logs remain available.
            from .process import stop_owned
            current = self.manager.current()
            if current.get("pid"):
                result = stop_owned(current, self.project)
                result["reason"] = str(error)
                result["error"] = error.envelope()["error"] if isinstance(error, InstanceError) else dict(message=str(error))
                return result
            raise
        finally:
            self.manager.close()
