"""Publish same-ID function changes and array retyping through strict receipts."""

from pathlib import Path
import re

from tests.live.collaboration.workflow import Workflow
from ue_node_nexus_mcp.transcode.storage.paths import text_path

BLUEPRINT = "/Game/Issues6/BP_Types.BP_Types"
MATERIAL = "/Game/Issues6/M_Compile.M_Compile"
BLUEPRINT_BODY = """[asset]
ParentClass = /Script/Engine.Actor

[variables]
Targets : Array<Object(/Script/Engine.PrimitiveComponent)>

[function AddFloat(A: float, B: float) -> (ReturnValue: float) {Pure, Public}]
entry.then -> result.execute
entry.A -> result.ReturnValue

[graph EventGraph]
number : CallFunction(/Script/Engine.KismetMathLibrary.Divide_DoubleDouble)
lerp : CallFunction(/Script/Engine.KismetMathLibrary.Lerp)
get : VariableGet(Targets)
loop : MacroInstance(StandardMacros.ForEachLoop)
clear : CallFunction(/Script/Engine.KismetArrayLibrary.Array_Clear)
add : CallFunction(/Script/Engine.KismetArrayLibrary.Array_AddUnique)
get.Targets -> loop.Array
get.Targets -> clear.TargetArray
get.Targets -> add.TargetArray
loop."Array Element" -> add.NewItem
"""
MATERIAL_BODY = """[asset]
ShadingModel = MSM_Unlit

[graph]
value : Constant(R=0.25)
value -> out.EmissiveColor
"""


def replace(workspace, asset, old, new):
    path = Path(workspace["file_paths"][asset])
    source = path.read_text(encoding="utf-8")
    assert old in source, (old, source)
    path.write_text(source.replace(old, new), encoding="utf-8", newline="\n")


def published(flow, workspace, label, *, allow_delete=False):
    flow.commit(workspace, label)
    result = flow.call("push", workspace, allow_delete=allow_delete)
    errors = dict((asset, dict(code=error.get("code"), message=error.get("message")))
                  for asset, error in result.get("errors", dict()).items())
    assert result["status"] == "published" and result["error_count"] == 0, (label, result["status"], errors)
    return result


def replace_call(workspace, before, after):
    path = Path(workspace["file_paths"][BLUEPRINT])
    source, count = re.subn(r"(?:/Script/Engine\.)?KismetMathLibrary\." + re.escape(before),
                            after, path.read_text(encoding="utf-8"))
    assert count == 1, (before, source)
    path.write_text(source, encoding="utf-8", newline="\n")


def bad_material(flow, author):
    path = Path(author["file_paths"][MATERIAL])
    before = path.read_text(encoding="utf-8")
    broken, count = re.subn(r"^value\s*:\s*Constant[^\n]*$", "value : AppendVector", before, flags=re.MULTILINE)
    assert count == 1, before
    path.write_text(broken, encoding="utf-8", newline="\n")
    flow.commit(author, "invalid AppendVector must roll back with diagnostics")
    result = flow.call("push", author, allow_delete=True)
    error = result.get("errors", dict()).get(MATERIAL, dict())
    assert result["error_count"] == 1 and error.get("code") == "apply_rolled_back", error
    assert "AppendVector" in str(error) and "Missing" in str(error), error
    flow.session.require("project_context_get")
    path.write_text(before, encoding="utf-8", newline="\n")
    repaired = published(flow, author, "restore valid material after rejected compilation", allow_delete=True)
    return dict(rejected=error, restored=repaired)


def exercise(session):
    flow = Workflow(session)
    flow.call("init", pull_all=False)
    author = flow.call("checkout", agent_id="issues6")
    for asset, kind, cls, body in ((BLUEPRINT, "blueprint", "Blueprint", BLUEPRINT_BODY),
                                   (MATERIAL, "material", "Material", MATERIAL_BODY)):
        path = text_path(Path(author["files_root"]), asset, kind)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"nexus: 1\nasset: {asset.split('.', 1)[0]}\nclass: {cls}\n"
                        f"schema: {author['schema_key']}\n\n{body}", encoding="utf-8", newline="\n")
    created = published(flow, author, "create typed fixtures")
    author = flow.call("checkout", agent_id="issues6-edit")
    replace_call(author, "Divide_DoubleDouble", "self.AddFloat")
    replace_call(author, "Lerp", "self.AddFloat")
    function = published(flow, author, "replace two functions under their existing IDs", allow_delete=True)
    replace(author, BLUEPRINT, "Array<Object(/Script/Engine.PrimitiveComponent)>", "Array<Struct(/Script/Engine.HitResult)>")
    arrays = published(flow, author, "change array element type under existing node IDs")
    replace(author, MATERIAL, "R=0.25", "R=0.5")
    material = published(flow, author, "compile changed shader and verify resource readiness")
    rejected_material = bad_material(flow, author)
    reopened = flow.call("checkout", paths=[BLUEPRINT, MATERIAL], agent_id="issues6-reopen")
    source = Path(reopened["file_paths"][BLUEPRINT]).read_text(encoding="utf-8")
    assert "HitResult" in source and "self.AddFloat" in source, source
    return dict(created=created, function=function, arrays=arrays, material=material, rejected_material=rejected_material,
                workspace=reopened["id"], mirror=str(flow.root))
