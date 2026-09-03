"""Synthetic raw exports shaped exactly like the UE plugin's transcode_export output."""

from __future__ import annotations

from typing import Any


def prop(name: str, type_name: str, value: str, default: str) -> dict[str, str]:
    return {"name": name, "type": type_name, "value": value, "default": default}


def material_function_raw() -> dict[str, Any]:
    def node(guid: str, cls: str, x: int, y: int, props: list[dict[str, str]], inputs: list[str], outputs: list[str]) -> dict[str, Any]:
        return {
            "guid": guid,
            "class": f"/Script/Engine.MaterialExpression{cls}",
            "class_short": cls,
            "name": f"MaterialExpression{cls}_0",
            "x": x,
            "y": y,
            "props": [prop("Desc", "FString", "", ""), *props],
            "inputs": inputs,
            "outputs": outputs,
        }

    return {
        "raw_version": 1,
        "asset_path": "/Game/WaterStains/Functions/MF_WS_S.MF_WS_S",
        "class": "/Script/Engine.MaterialFunction",
        "class_short": "MaterialFunction",
        "kind": "material_function",
        "schema_key": "5.5.4-abcd1234",
        "saved_hash": "h1",
        "dirty": False,
        "props": [
            prop("Description", "FString", "smoothstep", ""),
            prop("bExposeToLibrary", "bool", "True", "False"),
            prop("LibraryCategoriesText", "TArray<FText>", "()", "()"),
        ],
        "graph": {
            "nodes": [
                node("G-A", "FunctionInput", -1400, 0, [prop("InputName", "FName", "A", "In"), prop("InputType", "TEnumAsByte<EFunctionInputType>", "FunctionInput_Scalar", "FunctionInput_Vector3"), prop("SortPriority", "int32", "0", "32")], [""], [""]),
                node("G-B", "FunctionInput", -1400, 120, [prop("InputName", "FName", "B", "In"), prop("InputType", "TEnumAsByte<EFunctionInputType>", "FunctionInput_Scalar", "FunctionInput_Vector3"), prop("SortPriority", "int32", "1", "32")], [""], [""]),
                node("G-SUB", "Subtract", -1000, 80, [prop("Desc", "FString", "denom = B - A", ""), prop("ConstA", "float", "0.000000", "0.000000"), prop("ConstB", "float", "1.000000", "1.000000")], ["A", "B"], [""]),
                node("G-EPS", "Constant", -1000, 220, [prop("R", "float", "0.000001", "0.000000")], [], [""]),
                node("G-ADD", "Add", -800, 120, [prop("ConstA", "float", "0.000000", "0.000000"), prop("ConstB", "float", "1.000000", "1.000000")], ["A", "B"], [""]),
                node("G-TEX", "TextureSample", -800, 400, [prop("Texture", "UTexture", "/Game/T/T_Rock.T_Rock", "None")], ["UVs", "Tex", "ApplyViewMipBias", "Level"], ["", "", "", "", ""]),
                node("G-OUT", "FunctionOutput", -200, 40, [prop("OutputName", "FName", "Result", "Result")], [""], []),
            ],
            "links": [
                {"from": "G-B", "from_out": 0, "to": "G-SUB", "to_in": 0},
                {"from": "G-A", "from_out": 0, "to": "G-SUB", "to_in": 1},
                {"from": "G-SUB", "from_out": 0, "to": "G-ADD", "to_in": 0},
                {"from": "G-EPS", "from_out": 0, "to": "G-ADD", "to_in": 1},
                {"from": "G-TEX", "from_out": 1, "to": "G-OUT", "to_in": 0},
            ],
            "outputs": [],
        },
    }


def material_raw() -> dict[str, Any]:
    raw = material_function_raw()
    raw.update({
        "asset_path": "/Game/Materials/M_Glass.M_Glass",
        "class": "/Script/Engine.Material",
        "class_short": "Material",
        "kind": "material",
        "props": [prop("BlendMode", "TEnumAsByte<EBlendMode>", "BLEND_Translucent", "BLEND_Opaque"), prop("TwoSided", "bool", "False", "False")],
    })
    raw["graph"]["nodes"] = [n for n in raw["graph"]["nodes"] if n["class_short"] not in ("FunctionInput", "FunctionOutput")]
    raw["graph"]["links"] = [l for l in raw["graph"]["links"] if l["from"] not in ("G-A", "G-B") and l["to"] != "G-OUT"]
    raw["graph"]["outputs"] = [{"from": "G-ADD", "from_out": 0, "property": "BaseColor"}, {"from": "G-TEX", "from_out": 4, "property": "Opacity"}]
    return raw


def material_instance_raw() -> dict[str, Any]:
    return {
        "raw_version": 1,
        "asset_path": "/Game/Materials/MI_Glass_Soft.MI_Glass_Soft",
        "class": "/Script/Engine.MaterialInstanceConstant",
        "class_short": "MaterialInstanceConstant",
        "kind": "material_instance",
        "schema_key": "5.5.4-abcd1234",
        "saved_hash": "h2",
        "dirty": False,
        "props": [prop("Parent", "UMaterialInterface", "/Game/Materials/M_Glass.M_Glass", "None"), prop("PhysMaterial", "UPhysicalMaterial", "None", "None")],
        "instance": {
            "scalar": [{"name": "玻璃缩放", "type": "float", "value": "500.000000"}],
            "vector": [{"name": "Tint", "type": "FLinearColor", "value": "(R=1.000000,G=0.900000,B=0.800000,A=1.000000)"}],
            "texture": [{"name": "Normal", "type": "UTexture", "value": "/Game/T/T_N.T_N"}],
            "switch": [{"name": "UseMist", "type": "bool", "value": "True"}],
        },
    }


def blueprint_raw() -> dict[str, Any]:
    def pin(guid: str, name: str, direction: str, category: str, default: str = "", linked: list[str] | None = None, subobject: str = "", autogenerated: str | None = None) -> dict[str, Any]:
        return {"guid": guid, "name": name, "dir": direction, "type": {"category": category, "subcategory": "float" if category == "real" else "", "subobject": subobject, "container": "none", "is_ref": False}, "default": default, "autogenerated": autogenerated, "hidden": False, "linked": linked or []}

    return {
        "raw_version": 1,
        "asset_path": "/Game/Blueprints/BP_Door.BP_Door",
        "class": "/Script/Engine.Blueprint",
        "class_short": "Blueprint",
        "kind": "blueprint",
        "schema_key": "5.5.4-abcd1234",
        "saved_hash": "h3",
        "dirty": False,
        "props": [prop("BlueprintDescription", "FString", "A door", "")],
        "blueprint": {
            "parent_class": "/Script/Engine.Actor",
            "variables": [
                {"name": "Health", "guid": "V-1", "type": {"category": "real", "subcategory": "float", "subobject": "", "container": "none", "is_ref": False}, "default": "100.000000", "category": "Stats", "tooltip": "", "flags": ["InstanceEditable", "ExposeOnSpawn"], "rep_notify": ""},
                {"name": "Targets", "guid": "V-2", "type": {"category": "object", "subcategory": "", "subobject": "/Script/Engine.Actor", "container": "array", "is_ref": False}, "default": "", "category": "Default", "tooltip": "", "flags": ["Replicated"], "rep_notify": "OnRep_Targets"},
            ],
            "components": [
                {"name": "Root", "guid": "C-1", "class": "/Script/Engine.SceneComponent", "class_short": "SceneComponent", "parent": "", "socket": "", "inherited": False, "props": []},
                {"name": "Mesh", "guid": "C-2", "class": "/Script/Engine.StaticMeshComponent", "class_short": "StaticMeshComponent", "parent": "Root", "socket": "", "inherited": False, "props": [prop("StaticMesh", "UStaticMesh", "/Game/Meshes/SM_Door.SM_Door", "None"), prop("RelativeLocation", "FVector", "(X=0.000,Y=0.000,Z=50.000)", "(X=0.000,Y=0.000,Z=0.000)")]},
            ],
            "defaults": [prop("bReplicates", "bool", "True", "False"), prop("InitialLifeSpan", "float", "0.000000", "0.000000")],
            "dispatchers": [{"name": "OnOpened", "params": [{"name": "By", "type": {"category": "object", "subobject": "/Script/Engine.Actor"}}]}],
            "interfaces": ["/Game/Interfaces/BPI_Interactable.BPI_Interactable_C"],
            "graphs": [
                {
                    "name": "EventGraph",
                    "kind": "ubergraph",
                    "nodes": [
                        {"guid": "N-1", "class": "/Script/BlueprintGraph.K2Node_Event", "class_short": "Event", "x": 0, "y": 0, "title": "Event BeginPlay", "enabled": True, "comment": "", "config": {"function_owner": "/Script/Engine.Actor", "function_name": "ReceiveBeginPlay"}, "props": [], "pins": [pin("P-1", "then", "out", "exec", linked=["P-3"]), pin("P-1b", "OutputDelegate", "out", "delegate")], "supported": True},
                        {"guid": "N-2", "class": "/Script/BlueprintGraph.K2Node_VariableGet", "class_short": "VariableGet", "x": 300, "y": 120, "title": "Health", "enabled": True, "comment": "", "config": {"variable_name": "Health", "self_context": "true"}, "props": [], "pins": [pin("P-2", "Health", "out", "real", linked=["P-5"]), {"guid": "P-2s", "name": "self", "dir": "in", "type": {"category": "object", "subobject": "/Game/Blueprints/BP_Door.BP_Door_C"}, "default": "", "hidden": True, "linked": []}], "supported": True},
                        {"guid": "N-3", "class": "/Script/BlueprintGraph.K2Node_CallFunction", "class_short": "CallFunction", "x": 600, "y": 0, "title": "Print String", "enabled": True, "comment": "debug", "config": {"function_owner": "/Script/Engine.KismetSystemLibrary", "function_name": "PrintString", "self_context": "false"}, "props": [], "pins": [pin("P-3", "execute", "in", "exec", linked=["P-1"]), pin("P-4", "then", "out", "exec"), pin("P-4s", "InString", "in", "string", default="Hello", autogenerated="Hello"), pin("P-5", "Duration", "in", "real", default="2.000000", linked=["P-2"], autogenerated="2.000000"), pin("P-6", "bPrintToScreen", "in", "bool", default="true", autogenerated="true"), pin("P-7", "TextColor", "in", "struct", default="(R=0.000000,G=0.660000,B=1.000000,A=1.000000)", subobject="/Script/CoreUObject.LinearColor", autogenerated="(R=0.000000,G=0.660000,B=1.000000,A=1.000000)")], "supported": True},
                        {"guid": "N-4", "class": "/Script/BlueprintGraph.K2Node_Timeline", "class_short": "Timeline", "x": 900, "y": 0, "title": "Timeline_0", "enabled": True, "comment": "", "config": {}, "props": [], "pins": [pin("P-8", "Play", "in", "exec", linked=["P-4"]), pin("P-9", "Update", "out", "exec")], "supported": False, "t3d": "Begin Object Class=/Script/BlueprintGraph.K2Node_Timeline ... End Object"},
                    ],
                },
                {
                    "name": "TakeDamage",
                    "kind": "function",
                    "signature": {"inputs": [{"name": "Amount", "type": {"category": "real", "subcategory": "float"}}], "outputs": [{"name": "Dead", "type": {"category": "bool"}}], "flags": ["Public"], "category": "Combat", "locals": [{"name": "Remaining", "type": {"category": "real", "subcategory": "float"}, "default": "0.0"}]},
                    "nodes": [
                        {"guid": "F-1", "class": "/Script/BlueprintGraph.K2Node_FunctionEntry", "class_short": "FunctionEntry", "x": 0, "y": 0, "title": "TakeDamage", "enabled": True, "comment": "", "config": {}, "props": [], "pins": [pin("FP-1", "then", "out", "exec", linked=["FP-4"]), pin("FP-2", "Amount", "out", "real", linked=["FP-3"])], "supported": True},
                        {"guid": "F-2", "class": "/Script/BlueprintGraph.K2Node_CallFunction", "class_short": "CallFunction", "x": 300, "y": 0, "title": "<=", "enabled": True, "comment": "", "config": {"function_owner": "/Script/Engine.KismetMathLibrary", "function_name": "LessEqual_FloatFloat", "self_context": "false"}, "props": [], "pins": [pin("FP-3", "A", "in", "real", default="0.0", linked=["FP-2"]), pin("FP-3b", "B", "in", "real", default="0.0"), pin("FP-3c", "ReturnValue", "out", "bool", linked=["FP-5"])], "supported": True},
                        {"guid": "F-3", "class": "/Script/BlueprintGraph.K2Node_FunctionResult", "class_short": "FunctionResult", "x": 600, "y": 0, "title": "Return", "enabled": False, "comment": "", "config": {}, "props": [], "pins": [pin("FP-4", "execute", "in", "exec", linked=["FP-1"]), pin("FP-5", "Dead", "in", "bool", linked=["FP-3c"])], "supported": True},
                    ],
                },
            ],
        },
    }


def niagara_raw() -> dict[str, Any]:
    return {
        "raw_version": 1,
        "asset_path": "/Game/VFX/NS_Rain.NS_Rain",
        "class": "/Script/Niagara.NiagaraSystem",
        "class_short": "NiagaraSystem",
        "kind": "niagara_system",
        "schema_key": "5.5.4-abcd1234",
        "saved_hash": "h4",
        "dirty": False,
        "props": [prop("bFixedBounds", "bool", "True", "False"), prop("WarmupTime", "float", "0.500000", "0.000000")],
        "niagara": {
            "user_params": [{"name": "Spawn Rate", "type": "float", "value": "100.000000"}],
            "emitters": [
                {
                    "name": "Sparks",
                    "guid": "E-1",
                    "enabled": True,
                    "parent": "/Game/VFX/Emitters/E_Base.E_Base",
                    "props": [prop("SimTarget", "ENiagaraSimTarget", "CPUSim", "CPUSim")],
                    "stacks": [
                        {"group": "EmitterSpawn", "modules": []},
                        {"group": "EmitterUpdate", "modules": [{"guid": "M-1", "script": "/Niagara/Modules/Emitter/SpawnRate.SpawnRate", "script_short": "SpawnRate", "enabled": True, "inputs": [{"name": "SpawnRate", "type": "float", "value": "", "default": "10.0", "has_override": True, "linked": "User.Spawn Rate", "dynamic": False}]}]},
                        {"group": "ParticleSpawn", "modules": [{"guid": "M-2", "script": "/Niagara/Modules/Spawn/Initialization/InitializeParticle.InitializeParticle", "script_short": "InitializeParticle", "enabled": True, "inputs": [{"name": "Lifetime", "type": "float", "value": "2.000000", "default": "1.0", "has_override": True, "linked": None, "dynamic": False}, {"name": "Color", "type": "LinearColor", "value": "(R=1,G=0.5,B=0,A=1)", "default": "(R=1,G=1,B=1,A=1)", "has_override": False, "linked": None, "dynamic": False}]}]},
                        {"group": "ParticleUpdate", "modules": [{"guid": "M-3", "script": "/Niagara/Modules/Update/Forces/GravityForce.GravityForce", "script_short": "GravityForce", "enabled": False, "inputs": [{"name": "Gravity", "type": "Vector", "value": "(X=0,Y=0,Z=-980)", "default": "(X=0,Y=0,Z=-980)", "has_override": True, "linked": None, "dynamic": False}]}]},
                    ],
                    "opaque_stacks": [{"group": "Event:Collision", "t3d": "..."}],
                    "renderers": [{"guid": "R-1", "class": "/Script/Niagara.NiagaraSpriteRendererProperties", "class_short": "NiagaraSpriteRendererProperties", "props": [prop("Material", "UMaterialInterface", "/Game/VFX/M_Spark.M_Spark", "None"), prop("Alignment", "ENiagaraSpriteAlignment", "VelocityAligned", "Unaligned")]}],
                }
            ],
        },
    }
