"""Mount every captured preset and draw the real editor viewport on DX12."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import struct


def capture(session, name: str) -> dict:
    output = session.logs / "Renders" / f"{name}.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    result = session.require("viewport_capture", filename=session.name + "_" + name, target="level", show_ui=False, dry_run=False)
    captured = Path(result["file_path"])
    captured.resolve().relative_to(session.project.parent)
    assert result["exists"] and captured.is_file(), result
    shutil.copy2(captured, output)
    data = output.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", result
    width, height = struct.unpack(">II", data[16:24])
    assert width >= 320 and height >= 180, (width, height)
    return dict(file=str(output), width=width, height=height, sha256=hashlib.sha256(data).hexdigest())


def exercise(session, instances: list[str], map_path: str) -> dict:
    actors = session.require("level_actors_list", class_names=["PostProcessVolume"], format="full", limit=2000)
    assert not actors["has_more"], actors
    volumes = dict((row["actor_label"].removeprefix("PP_Forge_"), row["actor_path"])
                   for row in actors["items"] if row["actor_label"].startswith("PP_Forge_"))
    assert set(volumes) == set(("Oil", "Glyph", "Pixel", "Toon")), volumes
    for path in volumes.values():
        session.require("level_actor_properties_set", actor_path=path, properties=dict(bEnabled=False), dry_run=False)
    session.require("viewport_camera_set", location=dict(x=1100, y=-1700, z=750),
                    look_at=dict(x=-100, y=20, z=260), fov=52, dry_run=False)
    capture(session, "ReferenceWarmup")
    reference = capture(session, "Reference")
    records = []
    for asset in instances:
        family = asset.removeprefix("/Game/ForgePostProcess/").split("/", 1)[0]
        for name, path in volumes.items():
            properties = dict(bEnabled=name == family)
            if name == family:
                properties["Settings.WeightedBlendables"] = f'(Array=((Weight=1.0,Object="/Script/Engine.MaterialInstanceConstant\'{asset}\'")))'
            session.require("level_actor_properties_set", actor_path=path, properties=properties, dry_run=False)
        record = capture(session, asset.rsplit(".", 1)[-1])
        records.append(dict(asset=asset, **record))
    session.require("asset_save", asset_path=map_path, only_if_dirty=False)
    result = dict(reference=reference, presets=records)
    session.report("renders", result)
    return result
