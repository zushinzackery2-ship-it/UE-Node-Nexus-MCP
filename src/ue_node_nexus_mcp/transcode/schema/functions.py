"""Resolve functions through reflected owner inheritance, including the index."""

from ..errors import SyncError


def reference(lock, owner: str, name: str) -> str:
    full = owner + "." + name if owner else name
    if not owner.startswith("/Script/"):
        return full
    short_owner = owner.rsplit(".", 1)[-1]
    short = short_owner + "." + name
    if lock is None:
        return full
    try:
        record = lock.function(short_owner, name)
    except SyncError as exc:
        if exc.code != "schema_ambiguous":
            raise
        return full
    return short if record is not None and record["path"] == full else full


# ``coverage.functions`` of a catalog whose index also holds BlueprintEvent
# functions; older catalogs list only Blueprint-callable ones.
EVENT_INDEX = "registered_callable_and_event_index"
CALLABLE_INDEX = "registered_callable_index"


def resolve(lock, owner: str, name: str) -> dict | None:
    seen = set()
    while owner and owner not in seen:
        seen.add(owner)
        record = lock.direct_function(owner, name)
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
