from __future__ import annotations

import re
from pathlib import Path
from typing import Any

_DEFAULT_TAIL_BYTES = 512 * 1024
_MAX_LOG_ITEMS = 40

_COMPILE_RE = re.compile(
    r"\[AssetLog\]\s*(?P<asset>.*?):\s*Failed to compile "
    r"(?P<kind>Material Instance|Material)"
    r"(?: with Base (?P<base>.*?)(?: for platform|,))?.*Default Material will be used",
    re.IGNORECASE,
)
_TRANSIENT_COMPILE_RE = re.compile(
    r"\[AssetLog\]\s*(?P<asset>/Engine/Transient\.[^:]+):\s*Failed to compile "
    r"(?P<kind>Material Instance|Material)"
    r"(?: with Base (?P<base>.*?)(?: for platform|,))?.*Default Material will be used",
    re.IGNORECASE,
)
_SAMPLER_RE = re.compile(
    r"(?:Function\s+(?P<inline_function>[^:]+):\s*)?"
    r"\(Node\s+(?P<node>[^)]+)\)\s+Sampler type is\s+"
    r"(?P<actual>.*?),\s+should be\s+(?P<expected>.*?)\s+for\s+"
    r"(?P<texture>\S+)",
    re.IGNORECASE,
)
_FUNCTION_RE = re.compile(r"Function\s+(?P<function>[^:]+):\s*$", re.IGNORECASE)
_TIMESTAMP_RE = re.compile(r"^\[(?P<timestamp>\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]")


def enrich_with_material_log_diagnostics(
    response: dict[str, Any],
    project_context: dict[str, Any] | None,
    *,
    asset_path: str | None = None,
    severity: str = "all",
) -> dict[str, Any]:
    """Add recent UE material compile log entries as related diagnostics.

    UE log lines are historical, so these are deliberately not merged into
    data.items or data.error_count. They are high-signal context for failures
    like transient Landscape material permutations, but the current MessageLog
    remains the source of truth for active global counts.
    """
    log_text, log_path = read_latest_project_log(project_context)
    if not log_text:
        return response

    items = parse_material_log_diagnostics(log_text, asset_path=asset_path, severity=severity)
    if not items:
        return response

    data = response.setdefault("data", {})
    if not isinstance(data, dict):
        return response

    data["related_log_items"] = items
    data["related_log_item_count"] = len(items)
    data["related_log_source"] = str(log_path) if log_path else None
    data["related_log_stale_possible"] = True
    return response


def read_latest_project_log(project_context: dict[str, Any] | None, tail_bytes: int = _DEFAULT_TAIL_BYTES) -> tuple[str, Path | None]:
    log_dir = _project_log_dir(project_context)
    if log_dir is None or not log_dir.exists():
        return "", None

    candidates = sorted(
        (path for path in log_dir.glob("*.log") if path.is_file()),
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        return "", None

    path = candidates[0]
    size = path.stat().st_size
    with path.open("rb") as handle:
        if size > tail_bytes:
            handle.seek(size - tail_bytes)
        text = handle.read().decode("utf-8", errors="replace")
    return text, path


def parse_material_log_diagnostics(
    log_text: str,
    *,
    asset_path: str | None = None,
    severity: str = "all",
    limit: int = _MAX_LOG_ITEMS,
) -> list[dict[str, Any]]:
    if severity not in {"all", "error", "fatal"}:
        return []

    query = _asset_query_terms(asset_path)
    items: list[dict[str, Any]] = []
    seen: set[tuple[Any, ...]] = set()
    current: dict[str, Any] | None = None
    pending_function: str | None = None

    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        compile_match = _COMPILE_RE.search(line) or _TRANSIENT_COMPILE_RE.search(line)
        if compile_match:
            current = {
                "timestamp": _timestamp(line),
                "compiled_asset": compile_match.group("asset").strip(),
                "compile_kind": compile_match.group("kind"),
                "base_material": _clean_optional(compile_match.group("base")),
                "raw_compile_line": line,
            }
            pending_function = None
            if not _matches_query(current, query):
                current = None
            continue

        function_match = _FUNCTION_RE.search(line)
        if function_match:
            pending_function = function_match.group("function").strip()
            continue

        sampler_match = _SAMPLER_RE.search(line)
        if sampler_match and current is not None:
            item = {
                "severity": "error",
                "code": "material_sampler_type_mismatch_from_log",
                "source": "ue_log",
                "stale_possible": True,
                "message": (
                    "Material compile log reported sampler type "
                    f"{sampler_match.group('actual').strip()} but expected "
                    f"{sampler_match.group('expected').strip()}"
                ),
                "compiled_asset": current.get("compiled_asset"),
                "base_material": current.get("base_material"),
                "compile_kind": current.get("compile_kind"),
                "function": _clean_optional(sampler_match.group("inline_function")) or pending_function,
                "node": sampler_match.group("node").strip(),
                "texture": sampler_match.group("texture").strip(),
                "actual_sampler": sampler_match.group("actual").strip(),
                "expected_sampler": sampler_match.group("expected").strip(),
                "timestamp": current.get("timestamp"),
                "raw_compile_line": current.get("raw_compile_line"),
                "raw_detail_line": line,
            }
            if _matches_query(item, query):
                _append_unique(items, seen, item)
            pending_function = None
            continue

        if "Failed to compile Material" in line and current is not None:
            item = {
                "severity": "error",
                "code": "material_compile_failed_from_log",
                "source": "ue_log",
                "stale_possible": True,
                "message": "UE log reported a material compile fallback to Default Material",
                **current,
            }
            if _matches_query(item, query):
                _append_unique(items, seen, item)

    return items[-limit:]


def _project_log_dir(project_context: dict[str, Any] | None) -> Path | None:
    data = project_context.get("data") if isinstance(project_context, dict) else None
    if not isinstance(data, dict):
        return None

    saved_dir = data.get("project_saved_dir")
    if isinstance(saved_dir, str) and saved_dir:
        return Path(saved_dir) / "Logs"

    project_file = data.get("project_file_path")
    if isinstance(project_file, str) and project_file:
        return Path(project_file).parent / "Saved" / "Logs"
    return None


def _asset_query_terms(asset_path: str | None) -> set[str]:
    if not asset_path:
        return set()
    normalized = asset_path.replace("\\", "/").strip().lower()
    terms = {normalized}
    leaf = normalized.rsplit("/", 1)[-1]
    if leaf:
        terms.add(leaf)
        terms.add(leaf.split(".", 1)[0])
    return {term for term in terms if term}


def _matches_query(item: dict[str, Any], query: set[str]) -> bool:
    if not query:
        return True
    haystack = " ".join(str(value).lower() for value in item.values() if value is not None)
    return any(term in haystack for term in query)


def _append_unique(items: list[dict[str, Any]], seen: set[tuple[Any, ...]], item: dict[str, Any]) -> None:
    key = (
        item.get("code"),
        item.get("base_material"),
        item.get("function"),
        item.get("node"),
        item.get("texture"),
        item.get("actual_sampler"),
        item.get("expected_sampler"),
    )
    if key in seen:
        return
    seen.add(key)
    items.append(item)


def _timestamp(line: str) -> str | None:
    match = _TIMESTAMP_RE.search(line)
    if not match:
        return None
    return match.group("timestamp")


def _clean_optional(value: str | None) -> str | None:
    if value is None:
        return None
    cleaned = value.strip()
    return cleaned or None
