"""Select severe evidence under a fixed UTF-8 budget."""

import json

from ...diagnostic_counting import coerce_error_count

LEVELS = dict(fatal=4, error=3, warning=2, info=1)
STRING_LIMITS = dict(message=384, asset_path=256, node=128, graph=128, function=128,
                     source=64, code=64, node_guid=64, id=64, session_id=64,
                     first_at=40, last_at=40)
VALUE_FIELDS = ("occurrence_count", "sequence", "text_truncated")


def rank(item: dict) -> tuple:
    return (LEVELS.get(str(item.get("severity", "")).lower(), 0),
            str(item.get("last_at", "")), coerce_error_count(item.get("sequence")),
            coerce_error_count(item.get("occurrence_count")))


def ranked(items: list, limit: int = 3) -> list:
    return sorted((item for item in items if isinstance(item, dict)), key=rank, reverse=True)[:limit]


def compact(items: list, limit: int = 3, budget: int = 6144) -> list:
    samples = []
    used = 2
    for item in ranked(items, limit):
        sample = dict(severity=str(item.get("severity", "info")))
        truncated = bool(item.get("text_truncated")) or bool(item.get("sample_truncated"))
        for name, maximum in STRING_LIMITS.items():
            value = item.get(name)
            if isinstance(value, str):
                sample[name] = value[:maximum]
                truncated = truncated or len(value) > maximum
        for name in VALUE_FIELDS:
            if name in item:
                sample[name] = item[name]
        sample["text_truncated"] = truncated
        size = len(json.dumps(sample, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) + 1
        if used + size > budget:
            sample = dict((key, sample[key]) for key in
                          ("severity", "source", "code", "node_guid", "occurrence_count", "session_id") if key in sample)
            sample.update(message=str(item.get("message", ""))[:80], text_truncated=True)
            size = len(json.dumps(sample, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) + 1
        if used + size <= budget:
            samples.append(sample)
            used += size
    return samples
