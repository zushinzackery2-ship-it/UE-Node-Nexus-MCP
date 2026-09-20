#include "Patch/UeNodeNexusBridgeMaterialPatchConnectOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchContext.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchShared.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialPatchConnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FMaterialPatchContext& Context)
{
    const FString FromNodeId = ReadMaterialPatchNodeRef(Op, TEXT("from_node_id"), TEXT("from_node"));
    const FString ToNodeId = ReadMaterialPatchNodeRef(Op, TEXT("to_node_id"), TEXT("to_node"));
    FString FromPinId;
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("from_pin_id"), FromPinId))
    {
        Op->TryGetStringField(TEXT("from_pin"), FromPinId);
    }
    if (!Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        Op->TryGetStringField(TEXT("to_pin"), ToPinId);
    }
    if (FromNodeId.IsEmpty() || FromPinId.IsEmpty() || ToNodeId.IsEmpty() || ToPinId.IsEmpty())
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_connect_request"), TEXT("connect_pins requires from_node_id/from_pin_id/to_node_id/to_pin_id"), Material);
        return false;
    }

    UMaterialExpression* FromExpression = ResolveMaterialPatchNode(Material, FromNodeId, Context);
    if (FromExpression == nullptr)
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("source_node_not_found"), FString::Printf(TEXT("Source node not found: %s"), *FromNodeId), Material);
        return false;
    }

    bool bFromInput = false;
    int32 FromOutputIndex = INDEX_NONE;
    if (!ResolveMaterialOutputPin(FromExpression, FromPinId, bFromInput, FromOutputIndex) || bFromInput)
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_source_pin"), FString::Printf(TEXT("Source pin is not a valid output: node=%s pin=%s outputs=%s"), *FromNodeId, *FromPinId, *DescribeMaterialOutputPins(FromExpression)), Material);
        return false;
    }

    UMaterialExpression* ToExpression = nullptr;
    FExpressionInput* ToInput = nullptr;
    FString ResolvedToNodeId;
    EMaterialProperty ToMaterialProperty = MP_MAX;
    if (IsMaterialPatchOutputRef(ToNodeId))
    {
        if (!ResolveMaterialOutputProperty(ToPinId, ToMaterialProperty))
        {
            AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_material_output_pin"), FString::Printf(TEXT("Material output pin is not valid: %s"), *ToPinId), Material);
            return false;
        }
        ToInput = ResolveMaterialOutputInput(Material, ToPinId);
        ResolvedToNodeId = MaterialOutputNodeId();
    }
    else
    {
        ToExpression = ResolveMaterialPatchNode(Material, ToNodeId, Context);
        if (ToExpression == nullptr)
        {
            AddMaterialPatchDiagnostic(Diagnostics, TEXT("target_node_not_found"), FString::Printf(TEXT("Target node not found: %s"), *ToNodeId), Material);
            return false;
        }
        ToInput = ResolveMaterialInputPin(ToExpression, ToPinId);
        ResolvedToNodeId = MaterialExpressionNodeId(ToExpression);
    }
    if (FromExpression == nullptr || ToInput == nullptr || !FromExpression->GetOutputs().IsValidIndex(FromOutputIndex))
    {
        TArray<FString> AvailableInputs;
        if (ToExpression)
        {
            for (int32 i = 0; i < ToExpression->CountInputs(); ++i)
            {
                if (FString InputName = FindMaterialInputName(ToExpression, FString::Printf(TEXT("%s:in:%d"), *ResolvedToNodeId, i)); !InputName.IsEmpty())
                {
                    AvailableInputs.Add(InputName);
                }
            }
        }
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("pin_resolution_failed"), FString::Printf(TEXT("Could not resolve material link: %s.%s -> %s.%s. Available inputs on target: [%s]"), *FromNodeId, *FromPinId, *ToNodeId, *ToPinId, *FString::Join(AvailableInputs, TEXT(", "))), Material);
        return false;
    }

    if (ToMaterialProperty == MP_MaterialAttributes)
    {
        const bool bSourceIsMaterialAttributes = FromExpression->IsResultMaterialAttributes(FromOutputIndex);
        if (!bSourceIsMaterialAttributes)
        {
            AddMaterialPatchDiagnostic(Diagnostics, TEXT("material_pin_type_mismatch"), FString::Printf(TEXT("MaterialOutput.MaterialAttributes requires a material attributes output, but source %s.%s is %s"), *FromNodeId, *FromPinId, *MaterialPatchValueTypeName(FromExpression->GetOutputType(FromOutputIndex))), Material);
            return false;
        }
        if (!Material->bUseMaterialAttributes)
        {
            AddMaterialParamChange(Diff, MaterialOutputNodeId(), TEXT("bUseMaterialAttributes"), TEXT("False"), TEXT("True"));
            if (!bDryRun)
            {
                Material->bUseMaterialAttributes = true;
            }
        }
    }

    AppendMaterialDiff(Diff, TEXT("links_added"), MakeMaterialPatchLinkJson(MaterialExpressionNodeId(FromExpression), FromPinId, ResolvedToNodeId, ToPinId));
    if (!bDryRun)
    {
        if (ToExpression)
        {
            ToExpression->Modify();
        }
        ToInput->Connect(FromOutputIndex, FromExpression);
    }
    return true;
}

bool ApplyMaterialPatchDisconnect(UMaterial* Material, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, const FMaterialPatchContext& Context)
{
    const FString ToNodeId = ReadMaterialPatchNodeRef(Op, TEXT("to_node_id"), TEXT("to_node"));
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        Op->TryGetStringField(TEXT("to_pin"), ToPinId);
    }
    if (ToNodeId.IsEmpty() || ToPinId.IsEmpty())
    {
        return false;
    }

    UMaterialExpression* ToExpression = nullptr;
    FExpressionInput* Input = nullptr;
    FString ResolvedToNodeId;
    if (IsMaterialPatchOutputRef(ToNodeId))
    {
        Input = ResolveMaterialOutputInput(Material, ToPinId);
        ResolvedToNodeId = MaterialOutputNodeId();
    }
    else
    {
        ToExpression = ResolveMaterialPatchNode(Material, ToNodeId, Context);
        Input = ResolveMaterialInputPin(ToExpression, ToPinId);
        ResolvedToNodeId = MaterialExpressionNodeId(ToExpression);
    }
    if (Input == nullptr || Input->Expression == nullptr)
    {
        return false;
    }

    UMaterialExpression* FromExpression = Input->Expression;
    const FString FromNodeId = MaterialExpressionNodeId(FromExpression);
    const FString FromPinId = FString::Printf(TEXT("%s:out:%d"), *FromNodeId, Input->OutputIndex);
    AppendMaterialDiff(Diff, TEXT("links_removed"), MakeMaterialPatchLinkJson(FromNodeId, FromPinId, ResolvedToNodeId, ToPinId));
    if (!bDryRun)
    {
        if (ToExpression)
        {
            ToExpression->Modify();
        }
        Input->Expression = nullptr;
        Input->OutputIndex = 0;
    }
    return true;
}
}
