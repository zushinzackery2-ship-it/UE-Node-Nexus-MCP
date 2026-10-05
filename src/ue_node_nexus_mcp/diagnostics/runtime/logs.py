"""Parse historical PIE/script/RHI events without treating them as active counts."""

import re

LINE = re.compile(r"^\[(?P<time>[^]]+)\]\[\s*\d+\](?P<source>[^:]+):\s*(?P<severity>Warning|Error|Fatal):\s*(?P<text>.*)$")
LABELS = dict(node="Node|节点", graph="Graph|图表", function="Function|函数", asset_name="Blueprint|蓝图")
SOURCES = frozenset(("PIE", "LogScript", "LogRHI", "LogD3D12RHI", "LogShaderCompilers"))


def parse(log_text: str, *, asset_path: str | None = None, severity: str = "all", limit: int = 40) -> list[dict]:
    groups = dict()
    term = (asset_path or "").split(".")[-1].rsplit("/", 1)[-1].casefold()
    for line in log_text.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        source, level, text = match["source"], match["severity"].lower(), match["text"]
        if source == "LogWindows" and ("Fatal error:" in text or "Unhandled Exception:" in text):
            level = "fatal"
        elif source not in SOURCES:
            continue
        if severity != "all" and severity != level and not (severity == "error" and level == "fatal"):
            continue
        if term and term not in text.casefold():
            continue
        key = (source, level, text)
        if key in groups:
            item = groups.pop(key)
            item.update(occurrence_count=item["occurrence_count"] + 1, last_at=match["time"])
        else:
            item = dict(source="ue_log", category=source, severity=level, code="runtime_event_from_log",
                        stale_possible=True, message=text[:4096], first_at=match["time"], last_at=match["time"],
                        occurrence_count=1)
            for field, labels in LABELS.items():
                found = re.search(r"(?:" + labels + r")\s*[:：]\s*(.*?)"
                                  r"(?=\s+(?:Node|节点|Graph|图表|Function|函数|Blueprint|蓝图)\s*[:：]|$)", text)
                if found:
                    item[field] = found[1].strip()
            for field, pattern in (("pipeline_hash", r"combined hash (?:0x)?([A-Fa-f0-9]+)"),
                                   ("hresult", r"error ([A-Fa-f0-9]{8})"),
                                   ("vertex_shader_hash", r"Vertex: ([A-Fa-f0-9]{40})"),
                                   ("pixel_shader_hash", r"Pixel: ([A-Fa-f0-9]{40})")):
                found = re.search(pattern, text)
                if found:
                    item[field] = found[1]
        groups[key] = item
        if len(groups) > limit:
            groups.pop(next(iter(groups)))
    return list(groups.values())
