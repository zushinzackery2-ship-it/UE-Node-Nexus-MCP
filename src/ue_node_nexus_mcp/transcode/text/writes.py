"""Compare ImportText writes against only their explicitly supplied members."""

from .lexer import split_top_level
from .semantic import equivalent, value
from .values import unquote


def entries(text):
    source = str(text).strip()
    if not source.startswith("(") or not source.endswith(")"):
        return None
    return split_top_level(source[1:-1], ",") if source[1:-1].strip() else []


def members(items):
    result = dict()
    for item in items:
        name, separator, content = item.partition("=")
        name = name.strip()
        if not separator or not name or name in result:
            return None
        result[name] = content.strip()
    return result


def write_differences(expected, actual, contract, path=()):
    wanted, received = value(expected, contract), value(actual, contract)
    if equivalent(wanted, received):
        return []
    kind = contract.get("kind") if isinstance(contract, dict) else None
    left, right = entries(expected), entries(actual)
    if kind == "struct" and left is not None and right is not None:
        supplied, observed = members(left), members(right)
        if supplied is not None and observed is not None:
            rows = []
            for name, content in supplied.items():
                member = contract.get("fields", dict()).get(name, dict(type="text"))
                native = observed.get(name, member.get("default"))
                rows.extend(write_differences(unquote(content), unquote(native) if native is not None else None,
                                              member, (*path, name)))
            return rows
    if kind == "array" and left is not None and right is not None and len(left) == len(right):
        return [row for index, (content, native) in enumerate(zip(left, right))
                for row in write_differences(unquote(content.strip()), unquote(native.strip()),
                                             contract["element"], (*path, index))]
    return [dict(field=list(path), expected=wanted, actual=received)]
