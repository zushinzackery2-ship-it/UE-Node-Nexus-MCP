"""A project-sized dependency graph driven through the public sync workflow."""

from __future__ import annotations

import copy
import time
from pathlib import Path

from ue_node_nexus_mcp.transcode.sync.service import run_sync
from tests.collaboration.fake_bridge import ProtocolUe
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_raw, prop

FANOUT = 3
EDITED = "BLEND_Translucent", "BLEND_Opaque"


def texture(index: int) -> dict:
    path = f"/Game/T/T_{index:05d}.T_{index:05d}"
    raw = dict(raw_version=1, asset_path=path, kind="stub", class_short="Texture2D",
               schema_key=SCHEMA_KEY, saved_hash=f"t-{index}", props=[], tags=[])
    raw["class"] = "/Script/Engine.Texture2D"
    return raw


def material(index: int, textures: list[str]) -> dict:
    raw = copy.deepcopy(material_raw())
    raw["asset_path"] = f"/Game/M/M_{index:05d}.M_{index:05d}"
    nodes = raw["graph"]["nodes"]
    sample = next(node for node in nodes if node["class_short"] == "TextureSample")
    for slot, path in enumerate(textures):
        node = sample if slot == 0 else copy.deepcopy(sample)
        node["guid"], node["y"] = f"G-TEX{slot}", 400 + slot * 120
        node["props"] = [prop("Desc", "FString", "", ""), prop("Texture", "UTexture", path, "None")]
        if slot:
            nodes.append(node)
    return raw


def population(scale: int) -> dict:
    if scale < 10:
        raise ValueError("performance tiers require at least 10 assets")
    textures = [texture(index) for index in range(max(scale // 10, FANOUT))]
    paths = [raw["asset_path"] for raw in textures]
    assets = dict((raw["asset_path"], raw) for raw in textures)
    for index in range(scale - len(textures)):
        raw = material(index, [paths[(index * FANOUT + slot) % len(paths)] for slot in range(FANOUT)])
        assets[raw["asset_path"]] = raw
    return assets


class ScaleUe(ProtocolUe):
    """Counts editor work per asset and per round trip."""

    def __init__(self, assets: dict) -> None:
        super().__init__()
        self.assets = assets
        self.exported: list[str] = []
        self.export_calls = 0

    def op_transcode_export(self, payload):
        if payload.get("asset_paths"):
            self.export_calls += 1
            self.exported.extend(payload["asset_paths"])
        return super().op_transcode_export(payload)


class Project:
    def __init__(self, scale: int, root: Path) -> None:
        self.ue = ScaleUe(population(scale))
        self.env = dict(UE_NEXUS_TRANSCODE_DIR=str(root / "decoded"))
        self.entities = scale
        self.materials = sorted(path for path in self.ue.assets if "/Game/M/" in path)
        self.timings: dict[str, list[float]] = dict()
        start = time.perf_counter()
        self.workspace = run_sync(self.ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=self.env)
        self.checkout_seconds = time.perf_counter() - start

    def run(self, action: str, **options):
        return run_sync(self.ue, action, options=dict(dry_run=False, workspace_id=self.workspace["id"], **options), env=self.env)

    def timed(self, label: str, action: str, **options):
        start = time.perf_counter()
        result = self.run(action, **options)
        self.timings.setdefault(label, []).append(time.perf_counter() - start)
        return result

    def edit(self, asset: str, old: str = EDITED[0], new: str = EDITED[1]) -> None:
        path = Path(self.workspace["file_paths"][asset])
        text = path.read_text(encoding="utf-8")
        assert old in text, asset
        path.write_text(text.replace(old, new), encoding="utf-8")

    def counted(self) -> tuple[int, int]:
        return len(self.ue.exported), len(self.ue.applied)
