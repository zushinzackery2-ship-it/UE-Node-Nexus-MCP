"""Strict comparisons of native container defaults, floats and references."""

from ue_node_nexus_mcp.transcode.collaboration.semantic.values import normalize


def test_blueprint_linear_color_variable_retains_full_type_and_float32():
    from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture

    header = "nexus: 1\nasset: /Game/BP_Color\nclass: Blueprint\n[variables]\n"
    authored = header + "Tint : Struct(/Script/CoreUObject.LinearColor) = (R=0.4732,G=0.4966,B=0.52,A=1)\n"
    native = header + "Tint : Struct(/Script/CoreUObject.LinearColor) = (R=0.473199993,G=0.496600002,B=0.519999981,A=1)\n"
    before = capture(authored, None, "test", "blueprint")
    after = capture(native, before, "test", "blueprint")
    assert before["semantic"] == after["semantic"]
    assert before["semantic"] != capture(native.replace("0.519999981", "0.6"), before, "test", "blueprint")["semantic"]


def member(type_name, default):
    return dict(type=type_name, default=default)


def struct(fields, path=""):
    return dict(kind="struct", type="FOptions", fields=fields, path=path)


def test_array_struct_default_expansion_and_float32_identity():
    element = struct(dict(Name=member("FName", "None"), Amount=member("float", "0"),
                          Enabled=member("bool", "False")))
    contract = dict(kind="array", type="TArray<FOptions>", element=element)
    authored = '((Name="Test",Amount=0.7))'
    actual = '((Enabled=False,Amount=0.699999988,Name="Test"))'
    assert normalize(authored, contract) == normalize(actual, contract)
    assert normalize(authored, contract) != normalize(actual.replace("False", "True"), contract)
    assert normalize(authored, contract) != normalize(actual.replace("0.699999988", "0.7001"), contract)


def test_struct_nonzero_native_defaults():
    contract = struct(dict(bIgnoreAllPressedKeysUntilRelease=member("bool", "True"),
                           bForceImmediately=member("bool", "False")))
    assert normalize("()", contract) == normalize(
        "(bForceImmediately=False,bIgnoreAllPressedKeysUntilRelease=True)", contract)
    assert normalize("()", contract) != normalize("(bIgnoreAllPressedKeysUntilRelease=False)", contract)


def test_raw_property_contract_survives_capture_without_shared_schema():
    from ue_node_nexus_mcp.transcode.collaboration.semantic.normalization import normalize_snapshot
    from ue_node_nexus_mcp.transcode.collaboration.semantic.snapshot import capture, from_raw

    contract = dict(kind="array", type="TArray<FOptions>", element=struct(
        dict(Amount=member("float", "0"), Enabled=member("bool", "False"))))
    raw = dict(raw_version=1, kind="asset", asset_path="/Game/Options.Options", class_short="DataAsset", schema_key="key",
               props=[dict(name="Options", type=contract["type"], value_schema=contract,
                           value="((Amount=0.699999988,Enabled=False))")])
    native = from_raw(raw)
    authored = capture("nexus: 1\nasset: /Game/Options\nclass: DataAsset\nschema: key\n"
                       "[asset]\nOptions = ((Amount=0.7))\n", None, "author", "asset")
    expected = normalize_snapshot(authored, native)
    assert expected["semantic"] == native["semantic"]


def test_string_arrays_keep_element_boundaries_and_whitespace():
    contract = dict(kind="array", type="TArray<FString>", element=dict(type="FString"))
    assert normalize('( "a,b", " " )', contract) == '("a,b"," ")'
    assert normalize('("a,b")', contract) != normalize('("a","b")', contract)


def test_object_array_class_prefixes_preserve_identity_and_slot_order():
    contract = dict(kind="array", type="TArray<TObjectPtr<UMaterialInterface>>",
                    element=dict(kind="object", type="UMaterialInterface*"))
    authored = "(/Game/A.A,/Game/B.B)"
    native = '("/Script/Engine.MaterialInstanceConstant\'/Game/A.A\'","/Script/Engine.Material\'/Game/B.B\'")'
    assert normalize(authored, contract) == normalize(native, contract)
    assert normalize(authored, contract) != normalize("(/Game/B.B,/Game/A.A)", contract)


def test_material_overrides_compare_only_enabled_values():
    contract = struct(dict(bOverride_ShadingModel=member("bool", "False"),
                           ShadingModel=member("EMaterialShadingModel", "MSM_DefaultLit"),
                           bOverride_OpacityMaskClipValue=member("bool", "False"),
                           OpacityMaskClipValue=member("float", "0.333333")),
                      "/Script/Engine.MaterialInstanceBasePropertyOverrides")
    authored = "(bOverride_OpacityMaskClipValue=True,OpacityMaskClipValue=0.01)"
    actual = "(bOverride_ShadingModel=False,ShadingModel=MSM_Unlit,bOverride_OpacityMaskClipValue=True,OpacityMaskClipValue=0.00999999978)"
    assert normalize(authored, contract) == normalize(actual, contract)
    assert normalize(authored, contract) != normalize(actual.replace("0.00999999978", "0.1"), contract)
