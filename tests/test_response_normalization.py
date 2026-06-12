from __future__ import annotations

from ue_node_nexus_mcp.response_normalization import normalize_bridge_response


def test_nonfatal_material_attribute_normalization_does_not_mutate_input() -> None:
    response = {
        "ok": False,
        "operation": "graph_patch_apply",
        "data": {
            "applied": True,
            "post_checks": {
                "pin_integrity": {
                    "ok": False,
                    "broken_links": [{"reason": "bUseMaterialAttributes_false"}],
                    "missing_pins": [],
                },
                "compile": {"error_count": 0},
            },
        },
        "diagnostics": [],
        "warnings": [],
    }

    normalized = normalize_bridge_response(response)

    assert normalized is not response
    assert normalized["ok"] is True
    assert normalized["warnings"][0]["code"] == "nonfatal_material_attribute_pin_integrity"
    assert response["ok"] is False
    assert response["warnings"] == []
