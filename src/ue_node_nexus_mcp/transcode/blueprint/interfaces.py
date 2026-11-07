"""Blueprint signatures scoped to one candidate, including generated class aliases."""

from contextlib import contextmanager
from contextvars import ContextVar
from hashlib import sha256
import json

from .callables import local_records
from ..errors import SyncError
from ..storage.paths import package_name


_interfaces = ContextVar("blueprint_candidate_interfaces", default=None)


def signatures(items):
    result = {}
    for kind, document in items:
        if kind != "blueprint":
            continue
        path = package_name(document.header.asset)
        owner = path + "." + path.rsplit("/", 1)[-1] + "_C"
        asset = document.section("asset")
        parent = asset.prop_map().get("ParentClass") if asset else None
        result[path] = dict(parent=parent.value if parent else "", functions={
            name: dict(record, path=owner + "." + name) for name, record in local_records(document).items()})
    return result


def owner_record(owner):
    candidates = _interfaces.get()
    if candidates is None:
        return None
    if owner.startswith("/"):
        return candidates.get(package_name(owner))
    name = owner.removesuffix("_C")
    matches = [path for path in candidates if path.rsplit("/", 1)[-1] == name]
    if len(matches) > 1:
        raise SyncError("schema_ambiguous", f"ambiguous candidate Blueprint owner: {owner}", dict(candidates=matches))
    return candidates[matches[0]] if matches else None


def records(owner):
    record = owner_record(owner)
    return record["functions"] if record is not None else None


def fingerprint():
    candidates = _interfaces.get()
    return sha256(json.dumps(candidates, sort_keys=True).encode()).hexdigest() if candidates else ""


@contextmanager
def documents(items):
    token = _interfaces.set(signatures(items))
    try:
        yield
    finally:
        _interfaces.reset(token)
