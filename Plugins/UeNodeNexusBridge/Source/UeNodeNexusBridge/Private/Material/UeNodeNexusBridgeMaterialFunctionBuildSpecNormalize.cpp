#include "UeNodeNexusBridgeMaterialFunctionBuildSpecNormalize.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialFunction.h"
#include "Patch/UeNodeNexusBridgeMaterialFunctionPatchShared.h"

namespace UeNodeNexusBridge
{
static void CopyFunctionEndpointStringField(
    const TSharedPtr<FJsonObject>& Source,
    const FString& SourceName,
    const TSharedPtr<FJsonObject>& Target,
    const FString& TargetNodeField,
    const FString& TargetPinField)
{
    FString Value;
    if (!Source->TryGetStringField(SourceName, Value))
    {
        return;
    }

    FString NodeId;
    FString PinId;
    if (SplitMaterialEndpointShorthand(Value, NodeId, PinId))
    {
        Target->SetStringField(TargetNodeField, NodeId);
        Target->SetStringField(TargetPinField, PinId);
        return;
    }

    Target->SetStringField(TargetNodeField, Value);
}

static void CopyFunctionEndpointObjectFields(
    const TSharedPtr<FJsonObject>& Source,
    const TSharedPtr<FJsonObject>& Target,
    const FString& TargetNodeField,
    const FString& TargetPinField,
    const FString& PinAliasField)
{
    CopyFunctionPatchOptionalStringField(Source, TEXT("node"), Target, TargetNodeField);
    CopyFunctionPatchOptionalStringField(Source, TEXT("node_id"), Target, TargetNodeField);
    CopyFunctionPatchOptionalStringField(Source, TEXT("pin"), Target, TargetPinField);
    CopyFunctionPatchOptionalStringField(Source, TEXT("pin_id"), Target, TargetPinField);
    CopyFunctionPatchOptionalStringField(Source, PinAliasField, Target, TargetPinField);

    FString Ref;
    if (Source->TryGetStringField(TEXT("ref"), Ref))
    {
        FString NodeId;
        FString PinId;
        if (SplitMaterialEndpointShorthand(Ref, NodeId, PinId))
        {
            Target->SetStringField(TargetNodeField, NodeId);
            Target->SetStringField(TargetPinField, PinId);
        }
    }
}

static bool NormalizeFunctionBuildNodeSpecToCreateOp(const TSharedPtr<FJsonObject>& NodeSpec, TSharedPtr<FJsonObject>& OutOp)
{
    FString NodeClass;
    FString ClassPath;
    if (!NodeSpec->TryGetStringField(TEXT("node_class"), NodeClass))
    {
        NodeSpec->TryGetStringField(TEXT("class"), NodeClass);
    }
    if (!NodeSpec->TryGetStringField(TEXT("class_path"), ClassPath))
    {
        ClassPath = NodeClass;
    }
    if (ClassPath.IsEmpty())
    {
        return false;
    }

    OutOp = MakeShared<FJsonObject>();
    OutOp->SetStringField(TEXT("op"), TEXT("create_node"));
    OutOp->SetStringField(TEXT("class_path"), ClassPath);
    CopyFunctionPatchOptionalStringField(NodeSpec, TEXT("id"), OutOp, TEXT("client_id"));
    CopyFunctionPatchOptionalStringField(NodeSpec, TEXT("client_id"), OutOp, TEXT("client_id"));
    CopyFunctionPatchOptionalField(NodeSpec, TEXT("position"), OutOp, TEXT("position"));
    CopyFunctionPatchOptionalField(NodeSpec, TEXT("params"), OutOp, TEXT("params"));
    CopyFunctionPatchOptionalStringField(NodeSpec, TEXT("name"), OutOp, TEXT("name"));
    return true;
}

static bool NormalizeFunctionBuildLinkSpecToConnectOp(const TSharedPtr<FJsonObject>& LinkSpec, TSharedPtr<FJsonObject>& OutOp)
{
    OutOp = MakeShared<FJsonObject>();
    OutOp->SetStringField(TEXT("op"), TEXT("connect_pins"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("from_node_id"), OutOp, TEXT("from_node_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("from_node"), OutOp, TEXT("from_node_id"));
    CopyFunctionEndpointStringField(LinkSpec, TEXT("from"), OutOp, TEXT("from_node_id"), TEXT("from_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("from_pin_id"), OutOp, TEXT("from_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("from_pin"), OutOp, TEXT("from_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("output"), OutOp, TEXT("from_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("to_node_id"), OutOp, TEXT("to_node_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("to_node"), OutOp, TEXT("to_node_id"));
    CopyFunctionEndpointStringField(LinkSpec, TEXT("to"), OutOp, TEXT("to_node_id"), TEXT("to_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("to_pin_id"), OutOp, TEXT("to_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("to_pin"), OutOp, TEXT("to_pin_id"));
    CopyFunctionPatchOptionalStringField(LinkSpec, TEXT("input"), OutOp, TEXT("to_pin_id"));

    const TSharedPtr<FJsonObject>* From = nullptr;
    if (LinkSpec->TryGetObjectField(TEXT("from"), From) && From != nullptr)
    {
        CopyFunctionEndpointObjectFields(*From, OutOp, TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("output"));
    }
    const TSharedPtr<FJsonObject>* To = nullptr;
    if (LinkSpec->TryGetObjectField(TEXT("to"), To) && To != nullptr)
    {
        CopyFunctionEndpointObjectFields(*To, OutOp, TEXT("to_node_id"), TEXT("to_pin_id"), TEXT("input"));
    }
    return true;
}

void AppendMaterialFunctionBuildSpecOperations(
    const TSharedPtr<FJsonObject>& Payload,
    TArray<TSharedPtr<FJsonValue>>& OutOperations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UMaterialFunction* Function)
{
    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (Payload->HasField(TEXT("nodes")) && (!Payload->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr))
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_nodes"), TEXT("nodes must be an array"), Function);
    }
    else if (Nodes != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Nodes)
        {
            TSharedPtr<FJsonObject> NodeSpec = Value->AsObject();
            TSharedPtr<FJsonObject> Op;
            if (!NodeSpec.IsValid() || !NormalizeFunctionBuildNodeSpecToCreateOp(NodeSpec, Op))
            {
                AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_node"), TEXT("nodes entries must contain node_class/class_path and optional id/client_id"), Function);
                continue;
            }
            OutOperations.Add(MakeShared<FJsonValueObject>(Op));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
    if (Payload->HasField(TEXT("links")) && (!Payload->TryGetArrayField(TEXT("links"), Links) || Links == nullptr))
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_links"), TEXT("links must be an array"), Function);
    }
    else if (Links != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Links)
        {
            TSharedPtr<FJsonObject> LinkSpec = Value->AsObject();
            TSharedPtr<FJsonObject> Op;
            if (!LinkSpec.IsValid() || !NormalizeFunctionBuildLinkSpecToConnectOp(LinkSpec, Op))
            {
                AddFunctionPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_link"), TEXT("links entries must be objects"), Function);
                continue;
            }
            OutOperations.Add(MakeShared<FJsonValueObject>(Op));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* MaterialOutputs = nullptr;
    if (Payload->TryGetArrayField(TEXT("material_outputs"), MaterialOutputs) && MaterialOutputs != nullptr && MaterialOutputs->Num() > 0)
    {
        AddFunctionPatchDiagnostic(Diagnostics, TEXT("unsupported_material_outputs"), TEXT("material_function graph build uses FunctionOutput nodes and links; material_outputs is only valid for UMaterial"), Function);
    }
}
}
