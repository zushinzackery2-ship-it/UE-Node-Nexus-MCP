"""Verify the values an apply was asked to write before publishing its receipt."""

from ...storage.paths import object_path
from ...errors import SyncError
from ..semantic.normalization import normalize_snapshot
from ...text.semantic import equivalent, value
from ..workspace.files import filename


def entities(snapshot):
    result = dict()
    for scope, section in snapshot["semantic"]["sections"].items():
        for identifier, entity in section["entities"].items():
            result[(scope, entity["alias"])] = (identifier, entity)
    return result


def alias_index(items):
    result = dict()
    for key in items:
        result.setdefault(key[1], []).append(key)
    return result


def matching_keys(index, alias, graph):
    return [key for key in index.get(alias, ()) if graph_scope(key[0], graph)]


def differences(expected, actual, path=()):
    if equivalent(expected, actual):
        return []
    if isinstance(expected, dict) and isinstance(actual, dict) and "state" not in expected:
        return [row for key, item in expected.items() for row in differences(item, actual.get(key), (*path, key))]
    return [dict(field=list(path), expected=expected, actual=actual)]


def entity_fields(operation, expected, actual):
    verb = operation["op"]
    if verb in ("set_node_param", "bp_component_set_prop"):
        field = operation.get("prop", operation.get("name"))
        namespace = "props" if verb == "bp_component_set_prop" else "args"
        return differences(expected[namespace].get(field), actual[namespace].get(field), (namespace, field))
    if verb == "set_node_position":
        return differences(expected["position"], actual["position"], ("position",))
    if verb == "set_node_comment":
        return differences(expected["annotations"].get("comment", ""), actual["annotations"].get("comment", ""), ("annotations", "comment"))
    if verb == "set_node_enabled":
        return differences(expected["flags"], actual["flags"], ("flags",))
    fields = ["type", "positional", "args", "props"]
    if expected["default"].get("state") != "missing" or verb.startswith(("bp_variable_", "bp_local_variable_")):
        fields.append("default")
    if expected.get("position") is not None:
        fields.append("position")
    return [row for field in fields for row in differences(expected[field], actual[field], (field,))]


def property_result(operation, actual):
    verb, name = operation["op"], operation.get("name", "")
    section = "asset" if verb == "set_asset_prop" else "defaults"
    if verb == "mi_set_param":
        section = operation.get("section", operation.get("kind", ""))
    fields = actual["semantic"]["sections"].get(section + ":", dict()).get("props", dict())
    field = fields.get(name)
    rows = actual.get("raw", dict()).get("props", []) if section == "asset" else []
    if section == "defaults":
        rows = actual.get("raw", dict()).get("blueprint", dict()).get("defaults", [])
    native = next((row for row in rows if row.get("name") == name), None)
    contract = (native or dict()).get("value_schema") or (native or field or dict()).get("type", "text")
    if native:
        field = value(native.get("value"), contract)
    expected = value(operation.get("value"), contract)
    return differences(expected, field, (section, name))


def graph_scope(scope, graph):
    return not graph or scope.split(":", 1)[-1] == graph


def exact_fields(expected, actual, path):
    rows = differences(expected, actual, path)
    if isinstance(expected, dict) and isinstance(actual, dict):
        rows.extend(dict(field=[*path, key], expected=None, actual=actual[key]) for key in actual.keys() - expected.keys())
    return rows


def contract_result(operation, desired, actual):
    verb = operation["op"]
    if verb.startswith("bp_function_"):
        scope = "function:" + operation.get("name", "")
        expected = desired["semantic"]["sections"].get(scope)
        received = actual["semantic"]["sections"].get(scope)
        return differences(expected.get("args") if expected else None, received.get("args") if received else None, (scope, "signature"))
    section = "interfaces:" if verb.startswith("bp_interface_") else "dispatchers:"
    name = operation.get("interface", operation.get("name", ""))
    expected = desired["semantic"]["sections"].get(section, dict()).get("bare", dict()).get(name)
    received = actual["semantic"]["sections"].get(section, dict()).get("bare", dict()).get(name)
    return differences(expected, received, (section, name))


def verify_result(workspace, record, expected, actual) -> None:
    if expected is None:
        return
    desired = normalize_snapshot(expected, actual, workspace.schema)
    wanted, received = entities(desired), entities(actual)
    wanted_aliases, received_aliases = alias_index(wanted), alias_index(received)
    diagnostics = []
    checked_graphs = set()
    checked_selectors = set()
    asset = record["asset"]
    relative = workspace.state["files"].get(asset) or filename(object_path(asset), desired)
    file = str(workspace.root / relative)
    for operation in record["request"].get("plan", []):
        verb = operation.get("op", "")
        rows, line = [], operation.get("line") or 0
        if verb in ("set_asset_prop", "bp_default_set", "mi_set_param"):
            rows = property_result(operation, actual)
        elif verb == "mi_clear_param":
            scope = operation["kind"] + ":"
            field = actual["semantic"]["sections"].get(scope, dict()).get("props", dict()).get(operation["name"])
            rows = differences(None, field, (scope, operation["name"]))
        elif verb.startswith(("bp_interface_", "bp_dispatcher_", "bp_function_")):
            rows = contract_result(operation, desired, actual)
        elif verb in ("connect_pins", "disconnect_pins"):
            graph = operation.get("graph")
            if graph in checked_selectors:
                continue
            checked_selectors.add(graph)
            for scope, section in desired["semantic"]["sections"].items():
                if not graph_scope(scope, graph) or scope in checked_graphs:
                    continue
                checked_graphs.add(scope)
                incoming = actual["semantic"]["sections"].get(scope, dict()).get("links", dict())
                rows.extend(exact_fields(section["links"], incoming, (scope, "links")))
        else:
            alias = operation.get("id") or operation.get("new") or operation.get("name")
            graph = operation.get("graph") or operation.get("function")
            matches = matching_keys(wanted_aliases, alias, graph)
            if verb in ("delete_node", "bp_variable_remove", "bp_component_remove", "bp_local_variable_remove"):
                rows = [dict(field=[key[0], alias], expected=None, actual=received[key][1])
                        for key in matching_keys(received_aliases, alias, graph) if key not in wanted]
            for key in matches:
                identifier, entity = wanted[key]
                line = line or desired.get("locations", dict()).get(identifier, 0)
                found = received.get(key)
                rows.extend(entity_fields(operation, entity, found[1]) if found else
                            [dict(field=[key[0], alias], expected=entity, actual=None)])
        diagnostics.extend(dict(row, code="apply_result_mismatch", severity="error", asset=asset,
                                file=file, line=line, operation=verb) for row in rows)
    if diagnostics:
        raise SyncError("apply_result_mismatch", "UE readback differs from the requested writable state",
                        dict(apply_id=record["id"], asset=asset, diagnostics=diagnostics,
                             recover=dict(action="recover", options=dict(apply_id=record["id"], resolution="restore", dry_run=False))))
