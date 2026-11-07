"""Resolve functions through reflected owner inheritance, including the index."""

from ..errors import SyncError


def _function(lock, owner: str, name: str) -> dict | None:
    try:
        return lock.function(owner, name)
    except SyncError as exc:
        if exc.code != "schema_ambiguous":
            raise
        return None


def reference(lock, owner: str, name: str) -> str:
    """One function spelling: its declaring class, with full paths for assets.

    UE binds ``PlayerController.GetViewTarget`` to the UFunction declared on
    ``Controller`` and exports that owner, so an inherited spelling must reach
    the same text before authored and observed states are compared.
    """
    full = owner + "." + name if owner else name
    if lock is None or not owner or owner == "self":
        return full
    record = _function(lock, owner, name)
    if record is None:
        return full
    declared = record["path"]
    if not declared.startswith("/Script/"):
        return declared
    short_owner = record["path"].rpartition(".")[0].rsplit(".", 1)[-1]
    short = _function(lock, short_owner, name)
    return short_owner + "." + name if short is not None and short["path"] == declared else declared


# ``coverage.functions`` of a catalog whose index also holds BlueprintEvent
# functions; older catalogs list only Blueprint-callable ones.
EVENT_INDEX = "registered_callable_and_event_index"
CALLABLE_INDEX = "registered_callable_index"


def resolve(lock, owner: str, name: str) -> dict | None:
    return resolve_member(lock, owner, name, lock.direct_function, candidate_owners=True)


def resolve_member(lock, owner: str, name: str, direct, *, candidate_owners=False) -> dict | None:
    from ..blueprint.interfaces import owner_record

    seen = set()
    while owner and owner not in seen:
        seen.add(owner)
        candidate = owner_record(owner) if candidate_owners else None
        if candidate is not None:
            if name in candidate["functions"]:
                return candidate["functions"][name]
            owner = candidate["parent"]
            continue
        record = direct(owner, name)
        if record is not None:
            return record
        # Owners are written from native class paths, so only a path or a real
        # class name may answer; a stripped node alias would pick another class.
        info = next((item for family in ("component", "asset")
                     if (item := lock.resolve_class(family, owner, aliases=False)) is not None), None)
        owner = info.inheritance if info is not None else ""
    return None


def callable_record(record: dict | None) -> bool:
    """Index records from before ``callable`` existed were all Blueprint-callable."""
    return record is not None and record.get("callable", True)


def event_record(record: dict | None) -> bool:
    return record is not None and bool(record.get("event"))


def covers_events(lock) -> bool:
    return lock.info().get("coverage", dict()).get("functions") == EVENT_INDEX


def covers_calls(lock) -> bool:
    return "callable_functions" in lock.info().get("tables", dict())
