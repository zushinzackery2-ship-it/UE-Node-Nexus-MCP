"""Reuse the scene transport and collaboration receipt fixture together."""

from copy import deepcopy
import json
from pathlib import Path

from tests.collaboration.fake_bridge import ProtocolUe
from tests.scene.fake_scene import FakeScene
from ue_node_nexus_mcp.transcode.storage.io import digest


class SceneTransport(FakeScene):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner

    def _snapshot(self, payload):
        return self.owner.stamped(super()._snapshot(payload))


class SceneProtocol(ProtocolUe):
    def __init__(self):
        super().__init__()
        self.scene = SceneTransport(self)
        self.corrupt_readback = False
        self.retain_deleted = False

    def op_scene_export(self, payload):
        return self.scene.op_scene_export(payload)

    def op_scene_apply(self, payload):
        identifier = payload["apply_id"]
        request = dict(deepcopy(payload), operation="scene_apply")
        previous = self.receipts.get(identifier)
        if previous:
            assert previous["request_digest"] == digest(request)
            return dict(previous["response"]["data"], receipt=deepcopy(previous))
        before = deepcopy(self.scene.scene_actors)
        data = self.scene.op_scene_apply(payload)
        self.applied.append(deepcopy(payload))
        if not data["ok"]:
            self.scene.scene_actors = before
            self.scene.dirty_scene = False
            receipt = dict(apply_id=identifier, request=request, request_digest=digest(request), phase="rolled_back")
            self.receipts[identifier] = receipt
            return dict(__error__=dict(code="apply_rolled_back", message="scene save restored checkpoint"), receipt=receipt)
        raw = json.loads(Path(payload["out_file"]).read_text(encoding="utf-8"))
        after = self.stamped(raw)
        if self.retain_deleted:
            remaining = dict((actor["id"], actor) for actor in after["actors"])
            for actor_id, actor in before.items():
                if actor_id not in remaining:
                    after["actors"].append(actor)
                    continue
                owner = remaining[actor_id]
                kept = set(row["id"] for row in owner["components"])
                owner["components"].extend(row for row in actor["components"] if row["id"] not in kept)
            after = self.stamped(after)
        if self.corrupt_readback:
            after["actors"][0]["label"] = "Unexpected"
            after = self.stamped(after)
        receipt = dict(apply_id=identifier, request=request, request_digest=digest(request), phase="ue_committed",
                       after=after, response=dict(ok=True, data=deepcopy(data)))
        self.receipts[identifier] = receipt
        return dict(data, receipt=deepcopy(receipt))
