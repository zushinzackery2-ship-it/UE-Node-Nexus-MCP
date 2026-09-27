"""Resolve functions through reflected owner inheritance, including the index."""


def resolve(lock, owner: str, name: str) -> dict | None:
    seen = set()
    while owner and owner not in seen:
        seen.add(owner)
        record = lock.direct_function(owner, name)
        if record is not None:
            return record
        info = next((item for family in ("component", "asset")
                     if (item := lock.resolve_class(family, owner)) is not None), None)
        owner = info.inheritance if info is not None else ""
    return None
