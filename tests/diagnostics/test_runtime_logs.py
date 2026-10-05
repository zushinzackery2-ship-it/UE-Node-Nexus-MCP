from ue_node_nexus_mcp.diagnostics.runtime.logs import parse


def test_real_chinese_pie_shape_is_visible_and_repeated_errors_are_aggregated():
    line = '[2026.10.03-20.19.45:536][928]PIE: Error: 蓝图运行时错误："“无访问”正在尝试读取属性 MotionState"。 '
    line += '节点：  Set Turning 图表：  EventGraph 函数：  Execute Ubergraph ABP Character Pure 蓝图：  ABP_CharacterPure'
    items = parse("\n".join([line] * 6), asset_path="/Game/CharacterPure/ABP_CharacterPure.ABP_CharacterPure")
    assert len(items) == 1 and items[0]["occurrence_count"] == 6
    assert items[0]["node"] == "Set Turning" and items[0]["graph"] == "EventGraph"
    assert items[0]["asset_name"] == "ABP_CharacterPure" and items[0]["stale_possible"]


def test_english_shape_and_asset_filter():
    line = '[2026.10.03-20.19.45:536][928]PIE: Error: Blueprint Runtime Error: "Accessed None". Node: Set Turning Graph: EventGraph Function: Update Blueprint: ABP_Demo'
    assert parse(line, asset_path="/Game/Other.Other") == []
    assert parse(line)[0]["function"] == "Update"


def test_gpu_failure_preserves_pipeline_and_hresult():
    line = '[2026.10.03-19.53.56:168][  1]LogD3D12RHI: Error: Failed to create pipeline state with combined hash 07F94DA0EAF05112, error 80070057.'
    item = parse(line)[0]
    assert item["pipeline_hash"] == "07F94DA0EAF05112" and item["hresult"] == "80070057"


def test_history_limit_and_severity_are_bounded():
    log = "\n".join(f'[time][1]LogScript: Warning: problem {number}' for number in range(100))
    assert len(parse(log, limit=4)) == 4
    assert parse(log, severity="error") == []
