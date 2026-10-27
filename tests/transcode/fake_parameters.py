"""Instance overrides checked against what the parent chain exposes when they apply, as UE does."""

from __future__ import annotations

import copy

from ue_node_nexus_mcp.transcode.storage.paths import object_path

INSTANCE_PARENT_ERROR = "parent material exposes no {kind} parameter with that name"


def _value(node: dict, name: str) -> str:
    return next((str(row["value"]) for row in node.get("props") or [] if row["name"] == name), "")


class ExposedParameters:
    """Fake UE mixin: ``mi_set_param`` fails unless the instance's parent chain declares the name.

    A parameter is declared by a ``ScalarParameter`` node of the parent material
    or of a function it calls, read from the fake's memory at apply time.
    """

    def exposed(self, asset: str) -> set[str]:
        raw = self.assets.get(object_path(asset)) if asset and asset != "None" else None
        if raw is None:
            return set()
        names = set()
        for row in raw.get("props") or []:
            if row["name"] == "Parent":
                names |= self.exposed(row["value"])
        for node in (raw.get("graph") or dict()).get("nodes") or []:
            if node["class_short"] == "ScalarParameter":
                names.add(_value(node, "ParameterName"))
            elif node["class_short"] == "MaterialFunctionCall":
                names |= self.exposed(_value(node, "MaterialFunction"))
        return names

    def op_transcode_apply(self, payload: dict) -> dict:
        if payload["kind"] == "material_instance":
            exposed = self.exposed(payload["asset_path"])
            for index, verb in enumerate(payload.get("plan") or []):
                if verb["op"] == "mi_set_param" and verb["name"] not in exposed:
                    self.applied.append(copy.deepcopy(payload))
                    message = verb["name"] + ": " + INSTANCE_PARENT_ERROR.format(kind=verb["kind"])
                    return dict(applied=0, failed=[dict(index=index, code="param_failed", message=message)], diagnostics=[],
                                compile=dict(ran=False, ok=False, error_count=0), saved=False, file="", saved_hash="",
                                dirty=False, id_map=dict())
        return super().op_transcode_apply(payload)
