"""Protocol fixture with authoritative memory revisions and durable receipts."""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path

from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import raw_evidence
from ue_node_nexus_mcp.transcode.collaboration.store.io import canonical, digest
from tests.transcode.fake_ue import FakeUe, SCHEMA_KEY
from tests.transcode.fixtures import material_raw


class ProtocolUe(FakeUe):
    def __init__(self):
        material = material_raw()
        material["props"] = [row for row in material["props"] if row["name"] != "TwoSided"]
        material["props"].append(dict(name="TwoSided", type="bool", value="True", default="False"))
        texture = dict(raw_version=1, asset_path="/Game/T/T_Rock.T_Rock", kind="stub", class_short="Texture2D", schema_key=SCHEMA_KEY, props=[], tags=[])
        texture["class"] = "/Script/Engine.Texture2D"
        super().__init__(dict(material=material, texture=texture))
        self.epoch = "epoch-one"
        self.receipts = dict()
        self.repository = None
        self.before_apply = None
        self.after_apply = None
        self.lose_response = False
        self.fail_save = False

    def saving_fails(self, asset: str) -> bool:
        """``True`` fails every save; a set fails only the assets it names."""
        return self.fail_save is True or asset in (self.fail_save or ())

    def stamped(self, raw: dict) -> dict:
        value = deepcopy(raw)
        value["schema_key"] = self.schema_key
        value["dirty"] = raw.get("asset_path") in self.dirty
        value["content_revision"] = digest(raw_evidence(value))
        value["live_revision"] = self.epoch + ":" + value["content_revision"]
        value["revision"] = value["live_revision"]
        value["editor_epoch"] = self.epoch
        return value

    def _write_raw(self, out_dir, raw):
        file = super()._write_raw(out_dir, raw)
        Path(file).write_bytes(canonical(self.stamped(raw)))
        return file

    def op_transcode_root_set(self, payload):
        super().op_transcode_root_set(payload)
        if payload.get("collaboration_root"):
            if self.repository and self.repository != payload["collaboration_root"]:
                return dict(__error__=dict(code="repository_mismatch", message="different repository"))
            self.repository = payload["collaboration_root"]
        return dict(root=self.root, editor_epoch=self.epoch, collaboration_version=1)

    def op_transcode_recover(self, payload):
        receipt = self.receipts.get(payload["apply_id"])
        if not receipt:
            return dict(__error__=dict(code="apply_not_found", message="not found"))
        return dict(receipt=deepcopy(receipt))

    def call(self, operation, payload, **kwargs):
        if operation not in ("transcode_apply", "vfx_transcode_apply") or not payload.get("apply_id"):
            return super().call(operation, payload, **kwargs)
        identifier = payload["apply_id"]
        request = dict(deepcopy(payload), operation=operation)
        request_digest = digest(request)
        previous = self.receipts.get(identifier)
        if previous:
            if previous["request_digest"] != request_digest:
                return dict(ok=False, error=dict(code="idempotency_mismatch", message="different request"))
            return dict(ok=True, data=dict(previous["response"]["data"], receipt=deepcopy(previous)))
        if self.before_apply:
            callback, self.before_apply = self.before_apply, None
            callback()
        for role, expected in [("target", payload), *(("dependency", row) for row in payload.get("read_set", []))]:
            raw = self.assets.get(expected["asset_path"])
            live = self.stamped(raw)["live_revision"] if raw else ""
            matching = raw is None if expected.get("expected_absent") else raw and live == expected.get("expected_revision")
            if not matching:
                stale = dict(role=role, asset_path=expected["asset_path"], expected_revision=expected.get("expected_revision", ""),
                             expected_absent=bool(expected.get("expected_absent")), live_revision=live, exists=raw is not None)
                return dict(ok=False, error=dict(code="stale_target", message="revision changed: " + expected["asset_path"]),
                            data=dict(applied=0, stale=stale))
        before, before_dirty = deepcopy(self.assets), set(self.dirty)
        # Saving the package is part of the apply: the memory checkpoint is gone
        # once it succeeds, and comes back with the assets when it is rolled back.
        self.dirty.discard(payload["asset_path"])
        response = super().call(operation, payload, **kwargs)
        failed = response.get("data", dict()).get("failed")
        if failed or self.saving_fails(payload["asset_path"]):
            self.assets, self.dirty = before, before_dirty
            receipt = dict(apply_id=identifier, request=request, request_digest=request_digest, phase="rolled_back", response=response)
            self.receipts[identifier] = receipt
            return dict(ok=False, error=dict(code="apply_rolled_back", message="restored checkpoint"), data=dict(receipt=receipt))
        if payload.get("delete_asset"):
            self.assets.pop(payload["asset_path"], None)
            after = dict(asset_path=payload["asset_path"], exists=False, editor_epoch=self.epoch,
                         live_revision="", content_revision="", saved_hash="")
        else:
            after = self.stamped(self.assets[payload["asset_path"]])
        receipt = dict(apply_id=identifier, request=request, request_digest=request_digest, phase="ue_committed", after=after,
                       response=deepcopy(response), response_data=deepcopy(response["data"]))
        self.receipts[identifier] = receipt
        if self.after_apply:
            callback, self.after_apply = self.after_apply, None
            callback()
        if self.lose_response:
            self.lose_response = False
            raise OSError("response lost after save")
        return dict(response, data=dict(response["data"], receipt=deepcopy(receipt)))

    __call__ = call
