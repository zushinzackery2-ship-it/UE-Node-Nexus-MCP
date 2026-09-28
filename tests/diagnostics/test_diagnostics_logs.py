from __future__ import annotations

from ue_node_nexus_mcp.diagnostics_logs import parse_material_log_diagnostics


def _compile_line(asset_path: str, second: int) -> str:
    return (
        f"[2026.06.12-01.00.{second:02d}:000][201]LogMaterial: Warning: "
        f"[AssetLog] {asset_path}: Failed to compile Material for platform PCD3D_SM6, "
        "Default Material will be used in game."
    )


def _sampler_line(texture_path: str) -> str:
    return (
        "Function WorldAlignedTexture: (Node TextureSample) Sampler type is Color, "
        f"should be Linear Color for {texture_path}"
    )


def test_material_log_parser_limit_returns_latest_items() -> None:
    lines: list[str] = []
    for index in range(5):
        lines.extend(
            [
                _compile_line(f"/Game/Materials/M_{index}.M_{index}", index),
                _sampler_line(f"/Game/Textures/T_{index}.T_{index}"),
            ]
        )

    items = parse_material_log_diagnostics("\n".join(lines), limit=2)

    assert [item["compiled_asset"] for item in items] == [
        "/Game/Materials/M_3.M_3",
        "/Game/Materials/M_4.M_4",
    ]
    assert [item["texture"] for item in items] == [
        "/Game/Textures/T_3.T_3",
        "/Game/Textures/T_4.T_4",
    ]


def test_material_log_parser_keeps_distinct_sampler_mismatches() -> None:
    log_text = "\n".join(
        [
            _compile_line("/Game/Materials/M_Test.M_Test", 1),
            _sampler_line("/Game/Textures/T_A.T_A"),
            _sampler_line("/Game/Textures/T_B.T_B"),
        ]
    )

    items = parse_material_log_diagnostics(log_text)

    assert [item["texture"] for item in items] == [
        "/Game/Textures/T_A.T_A",
        "/Game/Textures/T_B.T_B",
    ]


def test_material_log_parser_filters_by_asset_path() -> None:
    log_text = "\n".join(
        [
            _compile_line("/Game/Materials/M_A.M_A", 1),
            _sampler_line("/Game/Textures/T_A.T_A"),
            _compile_line("/Game/Materials/M_B.M_B", 2),
            _sampler_line("/Game/Textures/T_B.T_B"),
        ]
    )

    items = parse_material_log_diagnostics(log_text, asset_path="/Game/Materials/M_B.M_B")

    assert len(items) == 1
    assert items[0]["compiled_asset"] == "/Game/Materials/M_B.M_B"
    assert items[0]["texture"] == "/Game/Textures/T_B.T_B"
