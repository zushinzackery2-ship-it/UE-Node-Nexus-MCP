"""Compare supplied FTransform members at Unreal's six-decimal ExportText precision."""

import math

from ..text.writes import entries, members


EXPORT_TEXT_ERROR = 0.5e-6


def fields(text):
    items = entries(text)
    return members(items) if items is not None else None


def differences(expected, actual, path=()):
    wanted, received = fields(expected), fields(actual)
    if wanted is None or received is None:
        return [dict(field=list(path), expected=expected, actual=actual)]
    rows = []
    for name, content in wanted.items():
        supplied = fields(content)
        observed = fields(received.get(name))
        if supplied is None or observed is None:
            rows.append(dict(field=[*path, name], expected=content, actual=received.get(name)))
            continue
        for axis, component in supplied.items():
            native = observed.get(axis)
            try:
                left, right = float(component), float(native)
            except (TypeError, ValueError):
                rows.append(dict(field=[*path, name, axis], expected=component, actual=native))
                continue
            # ExportText prints six decimal places; the original request retains its precision.
            tolerance = EXPORT_TEXT_ERROR + math.ulp(left) + math.ulp(right)
            if not math.isfinite(left) or not math.isfinite(right) or abs(left - right) > tolerance:
                rows.append(dict(field=[*path, name, axis], expected=left, actual=right,
                                 absolute_tolerance=tolerance))
    return rows
