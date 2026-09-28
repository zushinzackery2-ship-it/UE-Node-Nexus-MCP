"""Exercise candidates and reflected values through lint, commit and publish."""

from pathlib import Path

from ue_node_nexus_mcp.transcode.storage.paths import text_path
from tests.live.collaboration.workflow import Workflow


def write(workspace, key, name, kind, cls, body):
    asset = "/Game/Issues3/" + name
    path = text_path(Path(workspace["files_root"]), asset, kind)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"nexus: 1\nasset: {asset}\nclass: {cls}\nschema: {key}\n{body}", encoding="utf-8")
    return path


def publish(flow, workspace, label):
    lint = flow.call("lint", workspace)
    assert lint["error_count"] == 0, lint
    flow.commit(workspace, label)
    result = flow.call("push", workspace)
    assert result["status"] == "published" and result["error_count"] == 0, result
    return result


def exercise(session):
    flow = Workflow(session)
    workspace, _ = flow.prepare()
    key = workspace["schema_key"]
    function = write(workspace, key, "MF_Surface", "material_function", "MaterialFunction",
        "[graph]\nconstant : Constant(R=0.7)\nresult : FunctionOutput(OutputName=Old)\nconstant -> result.A\n")
    consumer = write(workspace, key, "M_Surface", "material", "Material",
        "[asset]\nShadingModel = MSM_Unlit\n[graph]\ncall : MaterialFunctionCall(MaterialFunction=/Game/Issues3/MF_Surface.MF_Surface)\ncall.Old -> out.EmissiveColor\n")
    write(workspace, key, "MI_Surface", "material_instance", "MaterialInstanceConstant",
        "[asset]\nParent = /Game/Issues3/M_Surface.M_Surface\n"
        "BasePropertyOverrides = (bOverride_BlendMode=True,BlendMode=BLEND_Masked,bOverride_OpacityMaskClipValue=True,OpacityMaskClipValue=0.01)\n")
    write(workspace, key, "MPC_Values", "asset", "MaterialParameterCollection",
        "[asset]\nScalarParameters = ((ParameterName=Amount,Id=00000001000000020000000300000004,DefaultValue=0.7))\n"
        "VectorParameters = ((ParameterName=Tint,Id=00000005000000060000000700000008,DefaultValue=(R=-0.6,G=1.8,B=0.52,A=1)))\n")
    write(workspace, key, "BP_Lights", "blueprint", "Blueprint",
        "[asset]\nParentClass = /Script/Engine.Actor\n[variables]\nSpeed : float = 0\nEnabled : bool = false\n"
        "Tint : Struct(/Script/CoreUObject.LinearColor) = (R=0.4732,G=0.4966,B=0.52,A=1)\n"
        "[components]\nLight : PointLightComponent {LightColor=(R=255,G=128,B=64,A=255)}\n"
        "[graph EventGraph]\nlabel : CallFunction(KismetStringLibrary.BuildString_Object, Suffix=\" \")\n"
        "[function UserConstructionScript()]\n")
    first = publish(flow, workspace, "Issues3: native defaults and creation")
    function.write_text(function.read_text(encoding="utf-8").replace("OutputName=Old", "OutputName=New"), encoding="utf-8")
    consumer.write_text(consumer.read_text(encoding="utf-8").replace("call.Old", "call.New"), encoding="utf-8")
    second = publish(flow, workspace, "Issues3: evolve existing local function interface")
    return dict(first=first, second=second, workspace=workspace["id"], mirror=str(flow.root))
