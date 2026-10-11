"""Measure retained Python containers once at admission, preserving aliases."""

from dataclasses import is_dataclass
import sys


def retained_bytes(value) -> int:
    pending, visited, total = [value], set(), 0
    while pending:
        item = pending.pop()
        identity = id(item)
        if identity in visited:
            continue
        visited.add(identity)
        total += sys.getsizeof(item)
        if isinstance(item, dict):
            pending.extend(item.keys())
            pending.extend(item.values())
        elif isinstance(item, (list, tuple, set, frozenset)):
            pending.extend(item)
        elif is_dataclass(item) and not isinstance(item, type):
            pending.append(vars(item))
    # Account for the two order indexes and this entry's bookkeeping as well.
    return total + 512
