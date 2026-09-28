"""Type-aware values. Text, inheritance and missing fields retain meaning."""

from __future__ import annotations

from .scalar import normalize as normalize_scalar
from .reflected import normalize_reflected


def normalize(text: str, type_name: str | dict) -> str:
    return normalize_reflected(text, type_name) if isinstance(type_name, dict) else normalize_scalar(text, type_name)


def value(text: str | None, type_name: str = "text", state: str = "explicit") -> dict:
    contract = type_name
    if isinstance(type_name, dict):
        type_name = type_name.get("type", "text")
    if text is None:
        return dict(state="missing", type=type_name)
    return dict(state=state, type=type_name, value=normalize(str(text), contract))


def render(item: dict) -> str | None:
    if item.get("state") in ("missing", "default"):
        return None
    return item.get("value")


def field_values(current: dict, types: dict, defaults: dict, previous: dict | None = None) -> dict:
    result = dict()
    before = previous or dict()
    for key in sorted(current.keys() | defaults.keys()):
        type_name = types.get(key) or before.get(key, dict()).get("type") or "text"
        state = "explicit" if key in current else "default"
        text = current.get(key) if key in current else defaults[key]
        result[key] = value(text, type_name, state)
        if key in defaults and result[key].get("value") == normalize(str(defaults[key]), type_name):
            result[key]["state"] = "default"
    return result


def equivalent(left, right) -> bool:
    """Effective values compare equally when only explicit/default syntax differs."""
    if left is right or left == right:
        return True
    if isinstance(left, dict) and isinstance(right, dict):
        if left.get("state") in ("explicit", "default") and right.get("state") in ("explicit", "default"):
            return dict((key, item) for key, item in left.items() if key != "state") == dict(
                (key, item) for key, item in right.items() if key != "state")
        return left.keys() == right.keys() and all(equivalent(item, right[key]) for key, item in left.items())
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(equivalent(a, b) for a, b in zip(left, right))
    return False
