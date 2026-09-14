"""Scale budgets for a project-sized workspace: work must follow the change.

One module run walks a single project through its whole life — cold import, a
first full worktree read, repeated idle inspection, one edit, one publication —
and asserts what each step is allowed to cost. Every budget is either a count
(exports, encodes, applies, lock acquisitions) or a ratio against this machine's
own measured cold import, so the file states invariants rather than hardware.

``UE_NEXUS_PERF_SCALE`` picks the tiers, default ``1000``; ``1000,10000`` runs
both. At 10% textures and three texture references per material a tier carries
0.9 * scale materials and 2.7 * scale relations. The project is built under
pytest's temporary root, so point ``--basetemp`` at a fast local volume before
running the large tier: per-file syscall latency dominates every measurement.
"""

from __future__ import annotations

import copy
import logging
import os
import time
import tracemalloc
from contextlib import contextmanager
from pathlib import Path

import pytest

from ue_node_nexus_mcp.transcode.collaboration.workspace import files as worktree
from ue_node_nexus_mcp.transcode.sync import run_sync
from tests.transcode.fake_ue import SCHEMA_KEY
from tests.transcode.fixtures import material_raw, prop

from .fake_bridge import ProtocolUe

TIERS = tuple(int(item) for item in os.environ.get("UE_NEXUS_PERF_SCALE", "1000").split(","))
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
    textures = [texture(index) for index in range(max(scale // 10, FANOUT))]
    paths = [raw["asset_path"] for raw in textures]
    assets = dict((raw["asset_path"], raw) for raw in textures)
    for index in range(scale - len(textures)):
        raw = material(index, [paths[(index * FANOUT + slot) % len(paths)] for slot in range(FANOUT)])
        assets[raw["asset_path"]] = raw
    return assets


class ScaleUe(ProtocolUe):
    """Counts the editor work a command asks for, per asset and per round trip."""

    def __init__(self, assets: dict) -> None:
        super().__init__()
        self.assets = dict(assets)
        self.exported: list[str] = []
        self.export_calls = 0

    def op_transcode_export(self, payload):
        if payload.get("asset_paths"):
            self.export_calls += 1
            self.exported.extend(payload["asset_paths"])
        return super().op_transcode_export(payload)


class Project:
    def __init__(self, ue: ScaleUe, env: dict, workspace: dict, entities: int) -> None:
        self.ue, self.env, self.workspace, self.entities = ue, env, workspace, entities
        self.materials = sorted(path for path in ue.assets if "/Game/M/" in path)
        self.timings: dict[str, list[float]] = dict()
        self.checkout_seconds = 0.0

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

    def percentiles(self, label: str) -> tuple[float, float]:
        ordered = sorted(self.timings[label])
        pick = lambda fraction: ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]
        return pick(0.5), pick(0.95)


@contextmanager
def encodes():
    """Count worktree re-encodes: the per-file cost the memo has to remove."""
    original, calls = worktree.capture, []
    worktree.capture = lambda *args, **options: (calls.append(args[0]), original(*args, **options))[1]
    try:
        yield calls
    finally:
        worktree.capture = original


@contextmanager
def locks():
    records: list[tuple] = []
    handler = logging.Handler()
    handler.emit = lambda record: records.append(record.args)
    logger = logging.getLogger("ue_nexus.lock")
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    try:
        yield records
    finally:
        logger.removeHandler(handler)


@contextmanager
def peak_bytes(result: list):
    tracemalloc.start()
    try:
        yield
    finally:
        result.append(tracemalloc.get_traced_memory()[1])
        tracemalloc.stop()


@pytest.fixture(scope="module", params=TIERS, ids=[f"{scale}-entities" for scale in TIERS])
def project(request, tmp_path_factory):
    scale = request.param
    assets = population(scale)
    ue = ScaleUe(assets)
    root = tmp_path_factory.mktemp(f"perf-{scale}")
    env = dict(UE_NEXUS_TRANSCODE_DIR=str(root / "decoded"))
    start = time.perf_counter()
    workspace = run_sync(ue, "checkout", options=dict(dry_run=False, agent_id="A"), env=env)
    built = Project(ue, env, workspace, len(assets))
    built.checkout_seconds = time.perf_counter() - start
    assert built.entities == scale
    return built


def test_a_cold_import_exports_each_asset_once_in_one_round_trip(project):
    assert sorted(project.ue.exported) == sorted(project.ue.assets)
    assert project.ue.export_calls == 1
    assert project.checkout_seconds / project.entities < 0.25


def test_observing_again_re_exports_nothing(project):
    before = project.counted()
    observed = project.timed("fetch", "fetch")
    assert project.counted() == before
    assert len(observed["revisions"]) == project.entities


def test_the_worktree_is_read_once_and_then_only_where_it_changed(project):
    with encodes() as calls:
        project.timed("status-cold", "status")
        assert len(calls) == project.entities
        calls.clear()
        project.timed("status", "status")
        assert calls == []
        project.edit(project.materials[0])
        project.timed("status-edited", "status")
        assert len(calls) == 1
    project.edit(project.materials[0], *reversed(EDITED))


def test_idle_inspection_does_not_track_project_size(project):
    for _ in range(7):
        project.timed("status", "status")
    median, tail = project.percentiles("status")
    assert tail < project.checkout_seconds / 8
    assert tail < median * 4 + 0.25


def test_publishing_one_edit_touches_one_asset(project):
    project.edit(project.materials[0])
    exports, applies = project.counted()
    project.timed("stage", "stage")
    project.timed("commit", "commit", all=True, message="one edit")
    result = project.timed("push", "push")
    assert result["status"] == "published", result
    assert [row["action"] for row in result["rows"]] == ["pushed"]
    # The editor already reported every other asset as saved and unchanged, so
    # publication re-reads nothing and applies only the asset that moved.
    assert project.counted() == (exports, applies + 1)
    assert project.timings["push"][0] < project.checkout_seconds / 2


def test_publication_serializes_once_and_local_commands_never_do(project):
    with locks() as records:
        project.timed("status", "status")
        project.edit(project.materials[1])
        project.timed("stage", "stage")
        project.timed("commit", "commit", all=True, message="second edit")
        local = [row for row in records if row[0] == "publication.lock"]
        records.clear()
        project.timed("push", "push")
        publication = [row for row in records if row[0] == "publication.lock"]
    assert local == []
    assert len(publication) == 1
    assert publication[0][2] < project.checkout_seconds / 2


def test_history_depth_leaves_the_hot_path_flat(project):
    before, _ = project.percentiles("status")
    for round_trip in range(5):
        project.edit(project.materials[2], *(EDITED if round_trip % 2 == 0 else EDITED[::-1]))
        project.run("commit", all=True, message=f"round {round_trip}")
    project.timings["deep"] = []
    for _ in range(5):
        project.timed("deep", "status")
    after, _ = project.percentiles("deep")
    assert after < before * 4 + 0.25


def test_peak_memory_follows_the_working_set(project):
    idle, published = [], []
    with peak_bytes(idle):
        project.run("status")
    project.edit(project.materials[3])
    project.run("commit", all=True, message="memory")
    with peak_bytes(published):
        project.run("push")
    assert idle[0] / project.entities < 8 * 1024
    assert published[0] / project.entities < 32 * 1024


def test_the_measured_budget_is_reported(project, record_property):
    relations = (project.entities - max(project.entities // 10, FANOUT)) * FANOUT
    measured = dict(entities=project.entities, relations=relations, exported_assets=len(project.ue.exported),
                    applied_assets=len(project.ue.applied), cold_checkout_seconds=round(project.checkout_seconds, 3))
    for label in sorted(project.timings):
        median, tail = project.percentiles(label)
        measured[f"{label}_p50"], measured[f"{label}_p95"] = round(median, 4), round(tail, 4)
    for name, value in measured.items():
        record_property(name, value)
    print("\nscale report: " + "  ".join(f"{name}={value}" for name, value in measured.items()))
