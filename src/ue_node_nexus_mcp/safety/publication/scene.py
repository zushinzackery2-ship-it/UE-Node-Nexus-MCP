"""Pin scene plans and name the physical map packages their worker can change."""

from copy import deepcopy
from pathlib import Path
from uuid import uuid4

from ...transcode.errors import SyncError
from ...transcode.storage.io import atomic_write, canonical
from ...transcode.storage.paths import content_parts


def packages(item: dict) -> set[str]:
    plan = item["scene_plan"]
    result = {plan["map_path"]}
    result.update(row["level_path"] for row in plan.get("actors", []))
    result.update(row["level_path"] for row in plan.get("managed_actors", []))
    for package in result:
        parts = content_parts(package)
        if parts is None or parts[0] != "Game":
            raise SyncError("isolation_target_rejected", "scene writes require copied /Game map packages",
                dict(asset=item["asset"], package=package))
    return result


def operation(project_file: str, item: dict) -> dict:
    packages(item)
    identifier = uuid4().hex
    plan = deepcopy(item["scene_plan"])
    plan["request_token"] = identifier
    relative = "Saved/Nexus/SafetyPlans/" + identifier + ".json"
    atomic_write(Path(project_file).resolve().parent / relative, canonical(plan))
    return dict(operation="scene_apply", payload=dict(plan_file=relative,
        out_file="Saved/Nexus/SafetyPlans/" + identifier + "-after.json", dry_run=False, save=True))
