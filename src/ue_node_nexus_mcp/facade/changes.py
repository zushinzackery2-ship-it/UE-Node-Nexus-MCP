"""Mutation facts from native receipts, independent of the requested batch size."""

FIELDS = ("created_assets", "modified_assets", "deleted_assets", "created_nodes", "modified_nodes", "deleted_nodes")
DIFF_FIELDS = {
    "assets_created": ("created_assets", "asset_path"),
    "assets_duplicated": ("created_assets", "destination_asset_path"),
    "assets_moved": ("modified_assets", "destination_asset_path"),
    "assets_renamed": ("modified_assets", "destination_asset_path"),
    "params_changed": ("modified_assets", "asset_path"),
    "nodes_created": ("created_nodes", "node_id"),
    "nodes_deleted": ("deleted_nodes", "node_id"),
}


def strings(values):
    return [value for value in values if isinstance(value, str) and value] if isinstance(values, list) else []


def evidence(payload, response, *, limit=None):
    data = response.get("data")
    data = data if isinstance(data, dict) else {}
    diff = data.get("diff")
    diff = diff if isinstance(diff, dict) else {}
    dry_run = payload.get("dry_run") is True or data.get("dry_run") is True
    changed = not dry_run and data.get("changed") is True
    changes = {key: set(strings(data.get(key, []))) if changed else set() for key in FIELDS}
    if changed:
        for field, (kind, key) in DIFF_FIELDS.items():
            rows = diff.get(field, [])
            if not isinstance(rows, list):
                continue
            changes[kind].update(row[key] for row in rows if isinstance(row, dict) and isinstance(row.get(key), str) and row[key])
        # A native changed receipt establishes that this single target changed.
        # A list of requested targets alone never identifies a successful subset.
        if not any(changes[key] for key in FIELDS[:3]) and isinstance(payload.get("asset_path"), str):
            changes["modified_assets"].add(payload["asset_path"])
        if not any(changes[key] for key in FIELDS[3:]) and isinstance(payload.get("node_id"), str):
            changes["modified_nodes"].add(payload["node_id"])
    affected = dict(assets=sorted(set.union(*(changes[key] for key in FIELDS[:3]))),
                    nodes=sorted(set.union(*(changes[key] for key in FIELDS[3:]))))
    created = set(strings(data.get("created_ids", []))) if changed else set()
    created.update(changes["created_assets"] | changes["created_nodes"])
    result = dict(changed=changed, dry_run=dry_run, affected=affected, created_ids=sorted(created),
                  changes={key: sorted(values) for key, values in changes.items()})
    for key in ("requested", "deleted_objects", "remaining_assets"):
        if key in data:
            result[key] = data[key]
    rows = diff.get("assets_deleted", [])
    if isinstance(rows, list) and rows:
        for key in ("packages_on_disk", "residual_packages"):
            result[key] = sorted({value for row in rows if isinstance(row, dict) for value in strings(row.get(key, []))})
    if limit is not None:
        result["change_counts"] = {key: len(values) for key, values in result["changes"].items()}
        result["changes_truncated"] = any(len(values) > limit for values in result["changes"].values())
        result["changes"] = {key: values[:limit] for key, values in result["changes"].items()}
        result["affected"] = {key: values[:limit] for key, values in affected.items()}
        for key in ("created_ids", "remaining_assets", "packages_on_disk", "residual_packages"):
            if isinstance(result.get(key), list):
                result[key] = result[key][:limit]
    return result
