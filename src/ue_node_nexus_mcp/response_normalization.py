from __future__ import annotations

from typing import Any

from .diagnostic_counting import coerce_error_count, count_diagnostic_errors


def _compile_post_check_is_clean(data: dict[str, Any]) -> bool:
    post_checks = data.get("post_checks")
    if not isinstance(post_checks, dict):
        return True
    compile_check = post_checks.get("compile")
    if not isinstance(compile_check, dict):
        return True
    return coerce_error_count(compile_check.get("error_count")) == 0


def _is_only_material_attribute_mode_pin_warning(response: dict[str, Any]) -> bool:
    if response.get("operation") != "graph_patch_apply" or response.get("ok") is not False:
        return False
    if isinstance(response.get("error"), dict):
        return False
    if count_diagnostic_errors(response.get("diagnostics")) > 0:
        return False

    data = response.get("data")
    if not isinstance(data, dict) or data.get("applied") is not True:
        return False
    if not _compile_post_check_is_clean(data):
        return False

    post_checks = data.get("post_checks")
    if not isinstance(post_checks, dict):
        return False
    pin_integrity = post_checks.get("pin_integrity")
    if not isinstance(pin_integrity, dict) or pin_integrity.get("ok") is not False:
        return False
    missing_pins = pin_integrity.get("missing_pins")
    if isinstance(missing_pins, list) and missing_pins:
        return False
    broken_links = pin_integrity.get("broken_links")
    if not isinstance(broken_links, list) or not broken_links:
        return False
    return all(
        isinstance(link, dict) and link.get("reason") == "bUseMaterialAttributes_false"
        for link in broken_links
    )


def normalize_bridge_response(response: dict[str, Any]) -> dict[str, Any]:
    if not _is_only_material_attribute_mode_pin_warning(response):
        return response

    normalized = dict(response)
    normalized["ok"] = True
    warnings = normalized.get("warnings")
    if warnings is None:
        warnings = []
    if isinstance(warnings, list):
        normalized["warnings"] = [
            *warnings,
            {
                "severity": "warning",
                "code": "nonfatal_material_attribute_pin_integrity",
                "message": "graph_patch_apply applied successfully; pin_integrity only reported an existing MaterialAttributes link while the material is not in Use Material Attributes mode.",
            },
        ]
    return normalized
