#include "Patch/UeNodeNexusBridgeMaterialPatchShared.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchContext.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeMaterialPatchLinkJson(const FString& FromNodeId, const FString& FromPinId, const FString& ToNodeId, const FString& ToPinId)
{
    TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
    Link->SetStringField(TEXT("from_node_id"), FromNodeId);
    Link->SetStringField(TEXT("from_pin_id"), FromPinId);
    Link->SetStringField(TEXT("to_node_id"), ToNodeId);
    Link->SetStringField(TEXT("to_pin_id"), ToPinId);
    return Link;
}

void AddMaterialPatchDiagnostic(TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FString& Code, const FString& Message, UMaterial* Material)
{
    Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), Code, Message, Material ? Material->GetPathName() : FString(), TEXT("UeNodeNexusBridge"))));
}

FString ReadMaterialPatchNodeRef(const TSharedPtr<FJsonObject>& Op, const FString& CanonicalField, const FString& AliasField)
{
    FString Value;
    if (Op->TryGetStringField(CanonicalField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    if (Op->TryGetStringField(AliasField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    return FString();
}

UMaterialExpression* ResolveMaterialPatchNode(UMaterial* Material, const FString& NodeId, const FMaterialPatchContext& Context)
{
    if (UMaterialExpression* const* Found = Context.ClientNodes.Find(NodeId))
    {
        return *Found;
    }
    return ResolveMaterialInterfaceNode(Material, NodeId);
}

bool IsMaterialPatchOutputRef(const FString& NodeId)
{
    return IsMaterialOutputNodeId(NodeId) || NodeId.Equals(TEXT("MaterialOutput"), ESearchCase::IgnoreCase) || NodeId.Equals(TEXT("output"), ESearchCase::IgnoreCase);
}

FString MaterialPatchValueTypeName(uint32 Type)
{
    if (Type == MCT_MaterialAttributes)
    {
        return TEXT("MaterialAttributes");
    }
    if (Type == MCT_Float1)
    {
        return TEXT("Float1");
    }
    if (Type == MCT_Float2)
    {
        return TEXT("Float2");
    }
    if (Type == MCT_Float3)
    {
        return TEXT("Float3");
    }
    if (Type == MCT_Float4)
    {
        return TEXT("Float4");
    }
    if (Type == MCT_Texture)
    {
        return TEXT("Texture");
    }
    if (Type == MCT_StaticBool)
    {
        return TEXT("StaticBool");
    }
    if (Type == MCT_ShadingModel)
    {
        return TEXT("ShadingModel");
    }
    if (Type == MCT_Unknown)
    {
        return TEXT("Unknown");
    }
    return FString::Printf(TEXT("0x%08x"), Type);
}
}
