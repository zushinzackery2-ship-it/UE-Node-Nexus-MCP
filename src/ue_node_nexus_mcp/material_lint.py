from __future__ import annotations

import re
from typing import Any

from .contracts import require_non_empty_string
from .runtime import call_bridge as _call
from .runtime import default_tool

_TEXTURE_NODE_TYPES = {"TextureSampleParameter2D", "TextureObjectParameter"}
_PLACEHOLDER_TEXTURE_MARKERS = ("T_UVChecker", "DefaultTexture", "DefaultDiffuse")
_ROUGHNESS_TERMS = ("roughness", "rgh", "_r.", "粗糙")
_COLOR_TERMS = ("color", "basecolor", "diffuse", "_c.", "颜色")
_NORMAL_TERMS = ("normal", "_n.", "nrm", "法线")


@default_tool()
def material_lint(
    asset_path: str,
    graph_kind: str = "material",
    max_texture_checks: int = 48,
) -> dict[str, Any]:
    """Run a Python-side read-only material lint without compiling the material."""
    require_non_empty_string(asset_path, "asset_path")
    graph = _call(
        "graph_node_info_get",
        {
            "asset_path": asset_path,
            "graph_kind": graph_kind,
            "section": "param",
            "format": "indexed",
            "id_mode": "both",
            "include_position": False,
        },
    )
    if graph.get("ok") is not True:
        return graph

    nodes = parse_indexed_material_texture_nodes(graph)
    texture_summaries = _read_texture_summaries(nodes, max_texture_checks=max_texture_checks)
    issues = lint_texture_nodes(nodes, texture_summaries)
    error_count = sum(1 for issue in issues if issue["severity"] == "error")
    warning_count = sum(1 for issue in issues if issue["severity"] == "warning")

    return {
        "ok": True,
        "operation": "material_lint",
        "data": {
            "asset_path": asset_path,
            "graph_kind": graph_kind,
            "checked_node_count": len(nodes),
            "checked_texture_count": len(texture_summaries),
            "issues": issues,
            "issue_count": len(issues),
            "error_count": error_count,
            "warning_count": warning_count,
            "passed": error_count == 0,
        },
        "diagnostics": [],
        "warnings": [],
    }


def parse_indexed_material_texture_nodes(graph_response: dict[str, Any]) -> list[dict[str, Any]]:
    data = graph_response.get("data")
    if not isinstance(data, dict):
        return []
    text = data.get("text")
    if not isinstance(text, str):
        return []

    type_names = _parse_index_line(text, "T")
    param_names = _parse_index_line(text, "P")
    aliases = _parse_node_aliases(text, type_names)
    values = _parse_node_values(text, param_names)
    real_ids = _parse_index_line(text, "R")

    nodes: list[dict[str, Any]] = []
    for alias_id, node in aliases.items():
        if node["node_type"] not in _TEXTURE_NODE_TYPES:
            continue
        params = values.get(alias_id, {})
        texture = _clean_exported_object_path(params.get("Texture"))
        sampler = params.get("SamplerType")
        if not texture or not sampler:
            continue
        nodes.append(
            {
                "alias_id": alias_id,
                "node_id": real_ids.get(alias_id),
                "node_alias": node["node_alias"],
                "node_type": node["node_type"],
                "parameter_name": params.get("ParameterName") or node["node_alias"],
                "texture": texture,
                "sampler_type": sampler,
                "group": params.get("Group"),
            }
        )
    return nodes


def lint_texture_nodes(
    nodes: list[dict[str, Any]],
    texture_summaries: dict[str, dict[str, Any]],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    for node in nodes:
        texture = node["texture"]
        summary = texture_summaries.get(texture, {})
        expected = _expected_sampler_type(node, summary)
        actual = node["sampler_type"]
        if expected and actual != expected:
            issues.append(
                _issue(
                    "error",
                    "material_sampler_type_mismatch",
                    f"{node['node_alias']} samples {texture} as {actual}, expected {expected}",
                    node,
                    expected_sampler=expected,
                    texture_summary=summary,
                )
            )

        if _is_placeholder_texture(texture):
            issues.append(
                _issue(
                    "warning",
                    "material_placeholder_texture",
                    f"{node['node_alias']} still uses placeholder texture {texture}",
                    node,
                    texture_summary=summary,
                )
            )
    return issues


def _read_texture_summaries(nodes: list[dict[str, Any]], *, max_texture_checks: int) -> dict[str, dict[str, Any]]:
    summaries: dict[str, dict[str, Any]] = {}
    for texture_path in dict.fromkeys(node["texture"] for node in nodes):
        if len(summaries) >= max_texture_checks:
            break
        response = _call("texture_summary_get", {"asset_path": texture_path, "format": "compact"})
        data = response.get("data")
        if isinstance(data, dict):
            summaries[texture_path] = data
    return summaries


def _expected_sampler_type(node: dict[str, Any], summary: dict[str, Any]) -> str | None:
    texture = str(node.get("texture", ""))
    sampler = str(node.get("sampler_type", ""))
    compression = str(summary.get("compression_settings", ""))
    source_format = str(summary.get("source_format", ""))
    srgb = summary.get("srgb")

    if _is_normal_node(node) and sampler != "SAMPLERTYPE_Normal":
        return "SAMPLERTYPE_Normal"
    if compression == "TC_Grayscale" or source_format == "TSF_G8":
        return "SAMPLERTYPE_Grayscale"
    if _is_roughness_node(node) and srgb is False and sampler == "SAMPLERTYPE_Color":
        return "SAMPLERTYPE_LinearColor"
    if _looks_like_color_texture(texture, node) and srgb is True and sampler == "SAMPLERTYPE_LinearColor":
        return "SAMPLERTYPE_Color"
    return None


def _parse_index_line(text: str, prefix: str) -> dict[str, str]:
    match = re.search(rf"^{re.escape(prefix)}:(.*)$", text, re.MULTILINE)
    if not match:
        return {}
    result: dict[str, str] = {}
    for entry in match.group(1).split(";"):
        if "=" not in entry:
            continue
        key, value = entry.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def _parse_node_aliases(text: str, type_names: dict[str, str]) -> dict[str, dict[str, str]]:
    match = re.search(r"^N:(.*)$", text, re.MULTILINE)
    if not match:
        return {}
    aliases: dict[str, dict[str, str]] = {}
    for entry in match.group(1).split(";"):
        parts = entry.split(":", 2)
        if len(parts) != 3:
            continue
        alias_id, type_id, node_alias = (part.strip() for part in parts)
        aliases[alias_id] = {
            "node_type": type_names.get(type_id, type_id),
            "node_alias": node_alias,
        }
    return aliases


def _parse_node_values(text: str, param_names: dict[str, str]) -> dict[str, dict[str, str]]:
    match = re.search(r"^V:(.*)$", text, re.MULTILINE)
    if not match:
        return {}
    values: dict[str, dict[str, str]] = {}
    for node_entry in match.group(1).split("|"):
        if ":" not in node_entry:
            continue
        alias_id, param_blob = node_entry.split(":", 1)
        params: dict[str, str] = {}
        for param_entry in param_blob.split(";"):
            if "=" not in param_entry:
                continue
            param_id, value = param_entry.split("=", 1)
            param_name = param_names.get(param_id.strip(), param_id.strip())
            params[param_name] = value.strip()
        values[alias_id.strip()] = params
    return values


def _clean_exported_object_path(value: str | None) -> str | None:
    if not value:
        return None
    cleaned = value.strip()
    match = re.search(r"'([^']+)'", cleaned)
    if match:
        return match.group(1)
    return cleaned


def _is_placeholder_texture(texture: str) -> bool:
    return any(marker in texture for marker in _PLACEHOLDER_TEXTURE_MARKERS)


def _is_roughness_node(node: dict[str, Any]) -> bool:
    return _has_any_term(node, _ROUGHNESS_TERMS)


def _is_normal_node(node: dict[str, Any]) -> bool:
    return _has_any_term(node, _NORMAL_TERMS)


def _looks_like_color_texture(texture: str, node: dict[str, Any]) -> bool:
    return _has_any_term({**node, "texture": texture}, _COLOR_TERMS)


def _has_any_term(node: dict[str, Any], terms: tuple[str, ...]) -> bool:
    haystack = " ".join(
        str(node.get(key, "")).lower()
        for key in ("node_alias", "parameter_name", "texture")
    )
    return any(term in haystack for term in terms)


def _issue(
    severity: str,
    code: str,
    message: str,
    node: dict[str, Any],
    **extra: Any,
) -> dict[str, Any]:
    return {
        "severity": severity,
        "code": code,
        "message": message,
        "node_alias": node.get("node_alias"),
        "node_id": node.get("node_id"),
        "parameter_name": node.get("parameter_name"),
        "texture": node.get("texture"),
        "sampler_type": node.get("sampler_type"),
        **extra,
    }
