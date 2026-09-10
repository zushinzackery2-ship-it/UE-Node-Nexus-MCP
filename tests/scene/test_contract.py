from copy import deepcopy

from ue_node_nexus_mcp.build_info.contract import CONTRACT_VERSION, CORE_MODULE, VERSION, VFX_MODULE, compatibility
from ue_node_nexus_mcp.payload_schema import payload_schema_for


def build_info():
    module = dict(version=VERSION, contract_version=CONTRACT_VERSION, loaded=True,
                  source_fingerprint="a" * 64, module_path="D:/Plugins/Bridge.dll", build_id="engine-build")
    return dict(build=dict(((CORE_MODULE, module), (VFX_MODULE, deepcopy(module)))))


def test_write_contract_requires_matching_loaded_binary():
    data = build_info()
    assert compatibility(data, "scene_apply") is None
    data["build"][CORE_MODULE]["contract_version"] -= 1
    assert compatibility(data, "scene_apply")["code"] == "bridge_contract_mismatch"


def test_vfx_requires_the_same_engine_build():
    data = build_info()
    data["build"][VFX_MODULE]["build_id"] = "other-engine-build"
    assert compatibility(data, "niagara_compile")["code"] == "bridge_build_id_mismatch"


def test_missing_identity_is_explicit():
    assert compatibility(dict(), "scene_apply")["code"] == "bridge_build_identity_missing"


def test_instance_schema_exposes_wire_ops_and_typed_items():
    schema = payload_schema_for("component_instances_patch")
    assert "ops" in schema["required"]
    assert schema["properties"]["ops"]["items"]["properties"]["op"]["enum"] == ["add", "update", "remove"]
    assert "plan_file" in payload_schema_for("scene_apply")["required"]
