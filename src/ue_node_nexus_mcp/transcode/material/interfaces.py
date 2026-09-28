"""Candidate-local function signatures; never mutate the persisted catalog."""

from contextlib import contextmanager
from contextvars import ContextVar
from hashlib import sha256
import json

from ..paths import object_path
from ..sync_project import SyncError


_interfaces = ContextVar("material_function_interfaces", default=None)


def lookup(path):
    records = _interfaces.get()
    return records.get(object_path(path)) if records is not None else None


def fingerprint():
    records = _interfaces.get()
    return sha256(json.dumps(records, sort_keys=True).encode()).hexdigest() if records else ""


def priority(item):
    text = item.pop("priority")
    try:
        return int(text)
    except ValueError:
        raise SyncError("invalid_sort_priority", f"MaterialFunction SortPriority must be an integer: {text}")


@contextmanager
def documents(items):
    records = dict()
    for kind, document in items:
        if kind != "material_function":
            continue
        graph = document.section("graph")
        ports = dict(inputs=[], outputs=[])
        for declaration in graph.decls() if graph else ():
            name = declaration.type_name.rsplit(".", 1)[-1].removeprefix("MaterialExpression")
            field = dict(FunctionInput="inputs", FunctionOutput="outputs").get(name)
            if not field:
                continue
            params = declaration.keyed()
            label = params.get("InputName" if field == "inputs" else "OutputName", "")
            ports[field].append(dict(name=label, priority=params.get("SortPriority", "0")))
        for entries in ports.values():
            entries.sort(key=priority)
        records[object_path(document.header.asset)] = ports
    token = _interfaces.set(records)
    try:
        yield
    finally:
        _interfaces.reset(token)


@contextmanager
def snapshots(items):
    from ..collaboration.semantic.decode import to_document

    with documents((item["semantic"]["kind"], to_document(item)) for item in items
                   if item and item["semantic"]["kind"] == "material_function"):
        yield


@contextmanager
def tree(store, entries):
    with snapshots(store.objects.data(identifier, "snapshot") for identifier in entries.values()):
        yield
