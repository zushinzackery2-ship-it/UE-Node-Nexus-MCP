"""Verify every requested scene write against the native saved sparse snapshot."""

from ..errors import SyncError
from ..storage.paths import content_parts, object_path
from ..text.writes import write_differences
from .identifiers import canonical_scene
from .transforms import differences as transform_differences


FIELDS = ("class", "level_path", "name", "label", "folder", "parent", "transform")


def written_fields(operation, row, path):
    rows = []
    for field in FIELDS:
        if field not in operation:
            continue
        wanted, received = operation[field], row.get(field)
        if field == "transform":
            rows.extend(transform_differences(wanted, received, (*path, field)))
            continue
        if field == "class":
            wanted = object_path(wanted) if content_parts(wanted) else wanted
            received = object_path(received) if isinstance(received, str) and content_parts(received) else received
        if field == "folder" and received == "None":
            received = ""
        rows.extend(write_differences(str(wanted), str(received) if received is not None else None,
                                     "string", (*path, field)))
    properties, defaults = row.get("properties", {}), row.get("defaults", {})
    for name, wanted in operation.get("properties", {}).items():
        # The native snapshot omits effective CDO defaults from its override dictionary.
        received = properties[name] if name in properties else defaults.get(name)
        definition = row.get("property_schema", {}).get(name, {})
        contract = definition.get("value_schema") or definition.get("type", "text")
        rows.extend(write_differences(wanted, received, contract, (*path, "properties", name)))
    return rows


def written_instances(operation, row, path):
    data = row.get("instance_data")
    if not isinstance(data, dict):
        return [dict(field=[*path, "instance_data"], expected="complete instance array", actual=data)]
    rows = write_differences(str(operation["custom_data_count"]), str(data["custom_data_count"]),
                             "int32", (*path, "custom_data_count"))
    supplied = {value["id"]: value for value in operation["instances"]}
    observed = {value["id"]: value for value in data["instances"]}
    if len(supplied) != len(operation["instances"]) or len(observed) != len(data["instances"]):
        raise SyncError("receipt_invalid", "scene instance identities must be unique")
    for identifier in supplied.keys() ^ observed.keys():
        rows.append(dict(field=[*path, "instances", identifier],
                         expected=supplied.get(identifier), actual=observed.get(identifier)))
    contract = dict(type="TArray", kind="array", element=dict(type="float"))
    for identifier in supplied.keys() & observed.keys():
        wanted, received = supplied[identifier], observed[identifier]
        prefix = (*path, "instances", identifier)
        rows.extend(transform_differences(wanted["transform"], received["transform"], (*prefix, "transform")))
        left = "(" + ",".join(str(value) for value in wanted["custom_data"]) + ")"
        right = "(" + ",".join(str(value) for value in received["custom_data"]) + ")"
        rows.extend(write_differences(left, right, contract, (*prefix, "custom_data")))
    if operation.get("read_only") != data.get("read_only"):
        rows.append(dict(field=[*path, "read_only"], expected=operation.get("read_only"),
                         actual=data.get("read_only")))
    return rows


def scene_result(workspace, desired, actual, record):
    plan = record["request"].get("scene_plan")
    if not isinstance(plan, dict) or not isinstance(plan.get("ops"), list):
        raise SyncError("receipt_invalid", "scene readback requires the committed inline plan",
                        dict(apply_id=record["id"]))
    plan = canonical_scene(plan)
    actors = {row["id"]: row for row in canonical_scene(actual.get("raw", {})).get("actors", [])}
    components, rows = {}, []
    for operation in plan["ops"]:
        verb, identifier = operation["op"], operation["id"]
        actor_id = operation.get("actor_id", identifier)
        actor = actors.get(actor_id)
        path = (verb, actor_id, identifier)
        if verb == "delete_actor":
            if actor is not None:
                rows.append(dict(field=list(path), expected=None, actual=actor))
            continue
        if verb in ("create_actor", "update_actor"):
            if actor is None:
                rows.append(dict(field=list(path), expected="saved actor", actual=None))
                continue
            rows.extend(written_fields(operation, actor, path))
            continue
        if verb not in ("create_component", "update_component", "remove_component", "instances_set"):
            raise SyncError("receipt_invalid", "scene readback received an unsupported write", dict(operation=verb))
        if actor_id not in components:
            components[actor_id] = {value["id"]: value for value in (actor or {}).get("components", [])}
        component = components[actor_id].get(identifier)
        if verb == "remove_component":
            if component is not None:
                rows.append(dict(field=list(path), expected=None, actual=component))
            continue
        if component is None:
            rows.append(dict(field=list(path), expected="saved component", actual=None))
            continue
        if verb == "instances_set":
            rows.extend(written_instances(operation, component, path))
            continue
        rows.extend(written_fields(operation, component, path))
    return rows
