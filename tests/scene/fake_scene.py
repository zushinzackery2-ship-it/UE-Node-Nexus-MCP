"""Scene file transport with controllable partial writes and delayed results."""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import uuid

from ..transcode.fake_ue import FakeUe
from .fixtures import MAP, actor, component, snapshot


class FakeScene(FakeUe):
    def __init__(self, assets: dict | None = None) -> None:
        super().__init__(assets or dict())
        self.scene_actors = dict((entry["id"], entry) for entry in snapshot()["actors"])
        self.unavailable: set[str] = set()
        self.fail_save = False
        self.fail_after: int | None = None
        self.before_result = None
        self.native_default = False
        self.reconstruct = False
        self.dirty_scene = False
        self.scene_plans: list[dict] = []

    def call(self, operation: str, payload: dict, **kwargs) -> dict:
        response = super().call(operation, payload, **kwargs)
        if operation == "scene_apply" and response.get("data", dict()).get("ok") is False:
            response["ok"] = False
            response["error"] = dict(code="scene_incomplete", message="controlled partial scene failure")
        return response

    __call__ = call

    def _snapshot(self, payload: dict) -> dict:
        refs = list(payload.get("actors", []))
        by_path = dict((entry["actor_path"], entry["id"]) for entry in self.scene_actors.values())
        refs.extend(dict(id=by_path.get(path, path), level_path=MAP) for path in payload.get("actor_paths", []))
        ids = set(ref["id"] for ref in refs)
        rows = [self.scene_actors[key] for key in sorted(ids) if key in self.scene_actors and key not in self.unavailable]
        missing = [ref for ref in refs if ref["id"] not in self.scene_actors and ref["id"] not in self.unavailable]
        unloaded = [ref for ref in refs if ref["id"] in self.unavailable]
        return snapshot(rows, payload["name"], missing, unloaded, self.dirty_scene)

    @staticmethod
    def _write(path: str, raw: dict) -> None:
        file = Path(path)
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(json.dumps(raw), encoding="utf-8")

    def op_scene_export(self, payload: dict) -> dict:
        raw = self._snapshot(payload)
        self._write(payload["out_file"], raw)
        return dict(file=payload["out_file"], revision=raw["revision"])

    def op_scene_status(self, payload: dict) -> dict:
        raw = self._snapshot(payload)
        raw["actor_count"] = len(raw.pop("actors"))
        return raw

    def op_scene_apply(self, payload: dict) -> dict:
        plan = json.loads(Path(payload["plan_file"]).read_text(encoding="utf-8"))
        self.scene_plans.append(plan)
        if self._snapshot(plan["selector"])["revision"] != plan["expected_revision"]:
            return dict(ok=False, error="revision conflict")
        if payload.get("dry_run", True):
            return dict(ok=True, dry_run=True, planned=len(plan["ops"]))
        for ref in plan["selector"].get("actors", []):
            owner = self.scene_actors.get(ref["id"])
            if owner:
                owner["scene_owner"] = f"{MAP}#{plan['name']}"
                for entry in owner["components"]:
                    if "instance_data" in entry:
                        entry["instance_data"]["identity_bound"] = True
        applied = 0
        for index, op in enumerate(plan["ops"]):
            if self.fail_after == index:
                break
            self._apply_scene_op(op, plan["name"])
            applied += 1
        ok = not self.fail_save and applied == len(plan["ops"])
        self.dirty_scene = not ok
        raw = snapshot(list(self.scene_actors.values()), plan["name"], dirty=self.dirty_scene)
        raw["request_token"] = plan["request_token"]
        if self.before_result:
            self.before_result()
        self._write(payload["out_file"], raw)
        return dict(ok=ok, applied=applied, changed=bool(applied), saved=ok and payload.get("save", True),
                    revision=raw["revision"], request_token=plan["request_token"], file=payload["out_file"],
                    saved_packages=[dict(package=MAP, saved=True)] if ok else [],
                    failed_packages=[] if ok else [dict(package="/Game/__ExternalActors__/fixture", saved=False)])

    def _apply_scene_op(self, op: dict, name: str) -> None:
        verb, identifier = op["op"], op["id"]
        if verb == "create_actor":
            entries = [component(uuid.uuid5(uuid.NAMESPACE_URL, identifier).hex, "NativeMesh", False)] if self.native_default else []
            owner = actor(identifier, "Spawned", entries)
            owner["class"] = op["class"]
            owner["scene_owner"] = f"{MAP}#{name}"
            self.scene_actors[identifier] = owner
            return
        if verb == "delete_actor":
            del self.scene_actors[identifier]
            return
        if verb == "update_actor":
            owner = self.scene_actors[identifier]
            owner["properties"].update(op.get("properties", dict()))
            for key in ("label", "folder", "parent", "transform"):
                if key in op:
                    owner[key] = op[key]
            if self.reconstruct:
                for entry in owner["components"]:
                    entry["properties"] = dict()
            return
        owner = self.scene_actors[op["actor_id"]]
        if verb == "create_component":
            entry = component(identifier, op["name"], "Instanced" in op["class"])
            entry["class"] = op["class"]
            entry["is_root"] = not owner["components"]
            entry["removable"] = not entry["is_root"]
            owner["components"].append(entry)
            return
        entry = next(row for row in owner["components"] if row["id"] == identifier)
        if verb == "instances_set":
            entry["instance_data"] = dict(custom_data_count=op["custom_data_count"], instances=deepcopy(op["instances"]),
                                          identity_bound=True, read_only=False)
        elif verb == "remove_component":
            owner["components"].remove(entry)
        elif verb == "update_component":
            entry["properties"].update(op.get("properties", dict()))
            for key in ("parent", "transform"):
                if key in op:
                    entry[key] = op[key]
        else:
            raise AssertionError(f"unhandled scene verb: {verb}")
