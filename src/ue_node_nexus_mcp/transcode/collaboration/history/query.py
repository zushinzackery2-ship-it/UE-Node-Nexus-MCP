"""Paged DAG queries, semantic diffs and field ancestry."""

from __future__ import annotations

from datetime import datetime, timezone

from .graph import History


def walk(history: History, head: str) -> list[str]:
    identifiers = history.ancestors(head)
    return sorted(identifiers, key=lambda item: (history.commit(item)["generation"], history.commit(item)["time"], item), reverse=True)


def moment(value) -> str:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return datetime.fromtimestamp(float(value), timezone.utc).isoformat()
    return str(value)


def touched(history: History, asset: str) -> set[str]:
    """Commits whose first-parent delta includes the asset, from the index."""
    with history.store.db.connection() as connection:
        return set(row[0] for row in connection.execute("SELECT commit_id FROM changes WHERE asset=?", (asset,)))


def log(history: History, head: str, limit: int = 50, cursor: str | None = None, asset: str | None = None,
        author: str | None = None, entity: str | None = None, since=None, until=None) -> dict:
    entries = walk(history, head)
    if cursor in entries:
        entries = entries[entries.index(cursor) + 1:]
    indexed = touched(history, asset) if asset else None
    matches = []
    for identifier in entries:
        if indexed is not None and identifier not in indexed:
            continue
        commit = history.commit(identifier)
        if author and author not in commit["author"]:
            continue
        if since and commit["time"] < moment(since):
            continue
        if until and commit["time"] > moment(until):
            continue
        if asset:
            current = history.entries(identifier).get(asset)
            if all(history.entries(parent).get(asset) == current for parent in commit["parents"] or [None]):
                continue
        if entity and not diff(history, commit["parents"][0] if commit["parents"] else None, identifier,
                               [asset] if asset else None, entity):
            continue
        matches.append(dict(commit, commit_id=identifier))
        if len(matches) >= max(1, min(limit, 1000)):
            break
    return dict(commits=matches, cursor=matches[-1]["commit_id"] if matches else None)


def changed_fields(before, after, prefix: tuple = ()) -> list[dict]:
    if before == after:
        return []
    if isinstance(before, dict) and isinstance(after, dict):
        result = []
        for key in sorted(before.keys() | after.keys()):
            result.extend(changed_fields(before.get(key, dict(state="missing")), after.get(key, dict(state="missing")), (*prefix, key)))
        return result
    return [dict(path=list(prefix), before=before, after=after)]


def diff(history: History, left: str, right: str, assets: list[str] | None = None,
         entity: str | None = None, field: str | None = None) -> list[dict]:
    before, after = history.entries(left), history.entries(right)
    result = []
    for asset in sorted(set(assets) if assets is not None else before.keys() | after.keys()):
        if before.get(asset) == after.get(asset):
            continue
        # An added or removed asset descends into an empty state so every field
        # keeps a real path; entity and field filters then apply to it as well.
        old = history.store.objects.data(before[asset], "snapshot")["semantic"] if asset in before else dict()
        new = history.store.objects.data(after[asset], "snapshot")["semantic"] if asset in after else dict()
        fields = changed_fields(old, new)
        fields = [item for item in fields if (not entity or entity in item["path"]) and (not field or field in item["path"])]
        if fields:
            result.append(dict(asset=asset, before=before.get(asset), after=after.get(asset), fields=fields))
    return result


def value_at(history: History, revision: str, asset: str, path: list[str]):
    identifier = history.entries(revision).get(asset)
    if identifier is None:
        return dict(state="missing")
    value = history.store.objects.data(identifier, "snapshot")["semantic"]
    for key in path:
        if not isinstance(value, dict) or key not in value:
            return dict(state="missing")
        value = value[key]
    return value


def blame(history: History, head: str, asset: str, path: list[str]) -> dict:
    expected = value_at(history, head, asset, path)
    pending, seen, sources = [head], set(), []
    while pending:
        identifier = pending.pop()
        if identifier in seen:
            continue
        seen.add(identifier)
        commit = history.commit(identifier)
        matching = [parent for parent in commit["parents"] if value_at(history, parent, asset, path) == expected]
        if matching:
            pending.extend(matching)
        else:
            sources.append(dict(commit_id=identifier, author=commit["author"], operation=commit["operation"], message=commit["message"]))
    return dict(asset=asset, field_path=path, value=expected, sources=sources)
