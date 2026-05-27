from __future__ import annotations

from pathlib import Path


def test_blueprint_node_create_uses_short_name_resolver() -> None:
    source = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Blueprint/"
        "UeNodeNexusBridgeBlueprintNodeWriteOps.cpp"
    ).read_text(encoding="utf-8")

    assert "ResolveBlueprintNodeClassForCreate" in source
    assert 'TEXT("/Script/BlueprintGraph.%s")' in source
    assert "LoadClass<UEdGraphNode>(nullptr, *NodeClassName)" not in source


def test_blueprint_event_node_requires_explicit_event_semantics() -> None:
    create_config = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Blueprint/"
        "UeNodeNexusBridgeBlueprintNodeCreateConfig.cpp"
    ).read_text(encoding="utf-8")
    node_params = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Dispatch/"
        "UeNodeNexusBridgeNodeClassParamsOps.cpp"
    ).read_text(encoding="utf-8")
    node_write = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Blueprint/"
        "UeNodeNexusBridgeBlueprintNodeWriteOps.cpp"
    ).read_text(encoding="utf-8")

    assert "ValidateBlueprintNodeCreateConfig" in create_config
    assert "NodeClass == UK2Node_Event::StaticClass()" in create_config
    assert "function_name and function_owner are required for K2Node_Event" in create_config
    assert 'TEXT("node_config_required")' in node_write
    assert 'TEXT("function_name")' in node_params
    assert 'TEXT("function_owner")' in node_params


def test_widget_blueprint_scs_unavailable_error_is_specific() -> None:
    source = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Blueprint/"
        "UeNodeNexusBridgeBlueprintComponentOps.cpp"
    ).read_text(encoding="utf-8")

    assert 'TEXT("blueprint_scs_unavailable")' in source
    assert 'TEXT("Blueprint with a SimpleConstructionScript could not be loaded")' not in source


def test_asset_get_reports_loaded_assets_not_visible_to_registry() -> None:
    asset_ops = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Assets/"
        "UeNodeNexusBridgeAssetOps.cpp"
    ).read_text(encoding="utf-8")
    loaded_status = Path(
        "Plugins/UeNodeNexusBridge/Source/UeNodeNexusBridge/Private/Assets/"
        "UeNodeNexusBridgeLoadedAssetStatus.cpp"
    ).read_text(encoding="utf-8")

    assert "LoadedAssetToJson" in asset_ops
    assert "FindObject<UObject>(nullptr, *NormalizedAssetPath)" in asset_ops
    assert 'TEXT("asset_registry_visible"), false' in loaded_status
    assert 'TEXT("package_dirty")' in loaded_status
