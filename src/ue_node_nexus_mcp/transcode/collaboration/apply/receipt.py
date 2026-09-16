"""Match native receipt requests independently of either side's hash encoding."""

import math

from ...sync_project import SyncError
from ..store.io import digest


def same_json(left, right) -> bool:
    if type(left) in (int, float) and type(right) in (int, float):
        return math.isfinite(left) and math.isfinite(right) and left == right
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(same_json(value, right[key]) for key, value in left.items())
    if isinstance(left, list):
        return len(left) == len(right) and all(same_json(a, b) for a, b in zip(left, right))
    return left == right


def verify_request(record: dict, receipt: dict) -> None:
    payload = record["request"]
    if digest(payload) != record["request_digest"]:
        raise SyncError("receipt_invalid", "the stored apply request failed its integrity check", dict(apply_id=record["id"]))
    # UE hashes its complete request with a native encoding. The local SHA-256
    # protects the stored payload; it is not the native receipt's digest.
    expected = dict(payload, operation=record["operation"])
    if not same_json(receipt.get("request"), expected):
        raise SyncError("receipt_invalid", "receipt answers a different request", dict(apply_id=record["id"]))
