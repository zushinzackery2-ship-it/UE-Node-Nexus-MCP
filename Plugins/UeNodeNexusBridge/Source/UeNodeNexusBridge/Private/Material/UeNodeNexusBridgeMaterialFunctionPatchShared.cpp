#include "UeNodeNexusBridgeMaterialFunctionPatchShared.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialFunctionPatchContext.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeFunctionPatchLinkJson(const FString& FromNodeId, const FString& FromPinId, const FString& ToNodeId, const FString& ToPinId)
{
    TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
    Link->SetStringField(TEXT("from_node_id"), FromNodeId);
    Link->SetStringField(TEXT("from_pin_id"), FromPinId);
    Link->SetStringField(TEXT("to_node_id"), ToNodeId);
    Link->SetStringField(TEXT("to_pin_id"), ToPinId);
    return Link;
}

void AddFunctionPatchDiagnostic(TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FString& Code, const FString& Message, UMaterialFunction* Function)
{
    Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), Code, Message, Function ? Function->GetPathName() : FString(), TEXT("UeNodeNexusBridge"))));
}

void CopyFunctionPatchOptionalStringField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName)
{
    FString Value;
    if (Source->TryGetStringField(SourceName, Value))
    {
        Target->SetStringField(TargetName, Value);
    }
}

void CopyFunctionPatchOptionalField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName)
{
    TSharedPtr<FJsonValue> Value = Source->TryGetField(SourceName);
    if (Value.IsValid())
    {
        Target->SetField(TargetName, Value);
    }
}

FString ReadFunctionPatchNodeRef(const TSharedPtr<FJsonObject>& Op, const FString& CanonicalField, const FString& AliasField)
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

UMaterialExpression* ResolveFunctionPatchNode(UMaterialFunction* Function, const FString& NodeId, const FMaterialFunctionPatchContext& Context)
{
    if (UMaterialExpression* const* Found = Context.ClientNodes.Find(NodeId))
    {
        return *Found;
    }
    return ResolveMaterialInterfaceNode(Function, NodeId);
}

bool SplitMaterialEndpointShorthand(const FString& Endpoint, FString& OutNodeId, FString& OutPinId)
{
    FString Text = Endpoint;
    Text.TrimStartAndEndInline();
    int32 DotIndex = INDEX_NONE;
    if (!Text.FindLastChar(TEXT('.'), DotIndex) || DotIndex <= 0 || DotIndex >= Text.Len() - 1)
    {
        return false;
    }

    OutNodeId = Text.Left(DotIndex);
    OutPinId = Text.Mid(DotIndex + 1);
    return !OutNodeId.IsEmpty() && !OutPinId.IsEmpty();
}
}
