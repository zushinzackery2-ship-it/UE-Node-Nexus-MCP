#include "UeNodeNexusBridgeMaterialBuildSpecNormalize.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "UeNodeNexusBridgeJson.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchShared.h"

namespace UeNodeNexusBridge
{
static void CopyOptionalStringField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName)
{
    FString Value;
    if (Source->TryGetStringField(SourceName, Value))
    {
        Target->SetStringField(TargetName, Value);
    }
}

static void CopyOptionalField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName)
{
    TSharedPtr<FJsonValue> Value = Source->TryGetField(SourceName);
    if (Value.IsValid())
    {
        Target->SetField(TargetName, Value);
    }
}

static bool SplitEndpointShorthand(const FString& Endpoint, FString& OutNodeId, FString& OutPinId)
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

static void CopyEndpointStringField(
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
    if (SplitEndpointShorthand(Value, NodeId, PinId))
    {
        Target->SetStringField(TargetNodeField, NodeId);
        Target->SetStringField(TargetPinField, PinId);
        return;
    }

    Target->SetStringField(TargetNodeField, Value);
}

static void CopyMaterialOutputEndpointStringField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target)
{
    FString Value;
    if (!Source->TryGetStringField(SourceName, Value))
    {
        return;
    }

    FString NodeId;
    FString PinId;
    if (SplitEndpointShorthand(Value, NodeId, PinId))
    {
        Target->SetStringField(TEXT("to_node_id"), NodeId);
        Target->SetStringField(TEXT("to_pin_id"), PinId);
        return;
    }

    Target->SetStringField(TEXT("to_pin_id"), Value);
}

static void CopyEndpointObjectFields(
    const TSharedPtr<FJsonObject>& Source,
    const TSharedPtr<FJsonObject>& Target,
    const FString& TargetNodeField,
    const FString& TargetPinField,
    const FString& PinAliasField)
{
    CopyOptionalStringField(Source, TEXT("node"), Target, TargetNodeField);
    CopyOptionalStringField(Source, TEXT("node_id"), Target, TargetNodeField);
    CopyOptionalStringField(Source, TEXT("pin"), Target, TargetPinField);
    CopyOptionalStringField(Source, TEXT("pin_id"), Target, TargetPinField);
    CopyOptionalStringField(Source, PinAliasField, Target, TargetPinField);

    FString Ref;
    if (Source->TryGetStringField(TEXT("ref"), Ref))
    {
        FString NodeId;
        FString PinId;
        if (SplitEndpointShorthand(Ref, NodeId, PinId))
        {
            Target->SetStringField(TargetNodeField, NodeId);
            Target->SetStringField(TargetPinField, PinId);
        }
    }
}

static bool NormalizeBuildNodeSpecToCreateOp(const TSharedPtr<FJsonObject>& NodeSpec, TSharedPtr<FJsonObject>& OutOp)
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
    CopyOptionalStringField(NodeSpec, TEXT("id"), OutOp, TEXT("client_id"));
    CopyOptionalStringField(NodeSpec, TEXT("client_id"), OutOp, TEXT("client_id"));
    CopyOptionalField(NodeSpec, TEXT("position"), OutOp, TEXT("position"));
    CopyOptionalField(NodeSpec, TEXT("params"), OutOp, TEXT("params"));
    CopyOptionalStringField(NodeSpec, TEXT("name"), OutOp, TEXT("name"));
    return true;
}

static bool NormalizeBuildLinkSpecToConnectOp(const TSharedPtr<FJsonObject>& LinkSpec, TSharedPtr<FJsonObject>& OutOp)
{
    OutOp = MakeShared<FJsonObject>();
    OutOp->SetStringField(TEXT("op"), TEXT("connect_pins"));
    CopyOptionalStringField(LinkSpec, TEXT("from_node_id"), OutOp, TEXT("from_node_id"));
    CopyOptionalStringField(LinkSpec, TEXT("from_node"), OutOp, TEXT("from_node_id"));
    CopyEndpointStringField(LinkSpec, TEXT("from"), OutOp, TEXT("from_node_id"), TEXT("from_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("from_pin_id"), OutOp, TEXT("from_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("from_pin"), OutOp, TEXT("from_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("output"), OutOp, TEXT("from_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("to_node_id"), OutOp, TEXT("to_node_id"));
    CopyOptionalStringField(LinkSpec, TEXT("to_node"), OutOp, TEXT("to_node_id"));
    CopyEndpointStringField(LinkSpec, TEXT("to"), OutOp, TEXT("to_node_id"), TEXT("to_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("to_pin_id"), OutOp, TEXT("to_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("to_pin"), OutOp, TEXT("to_pin_id"));
    CopyOptionalStringField(LinkSpec, TEXT("input"), OutOp, TEXT("to_pin_id"));

    const TSharedPtr<FJsonObject>* From = nullptr;
    if (LinkSpec->TryGetObjectField(TEXT("from"), From) && From != nullptr)
    {
        CopyEndpointObjectFields(*From, OutOp, TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("output"));
    }
    const TSharedPtr<FJsonObject>* To = nullptr;
    if (LinkSpec->TryGetObjectField(TEXT("to"), To) && To != nullptr)
    {
        CopyEndpointObjectFields(*To, OutOp, TEXT("to_node_id"), TEXT("to_pin_id"), TEXT("input"));
    }
    return true;
}

static bool NormalizeBuildMaterialOutputSpecToConnectOp(const TSharedPtr<FJsonObject>& OutputSpec, TSharedPtr<FJsonObject>& OutOp)
{
    OutOp = MakeShared<FJsonObject>();
    OutOp->SetStringField(TEXT("op"), TEXT("connect_pins"));
    OutOp->SetStringField(TEXT("to_node_id"), MaterialOutputNodeId());
    CopyOptionalStringField(OutputSpec, TEXT("from_node_id"), OutOp, TEXT("from_node_id"));
    CopyOptionalStringField(OutputSpec, TEXT("from_node"), OutOp, TEXT("from_node_id"));
    CopyEndpointStringField(OutputSpec, TEXT("from"), OutOp, TEXT("from_node_id"), TEXT("from_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("from_pin_id"), OutOp, TEXT("from_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("from_pin"), OutOp, TEXT("from_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("output"), OutOp, TEXT("from_pin_id"));
    CopyMaterialOutputEndpointStringField(OutputSpec, TEXT("to"), OutOp);
    CopyOptionalStringField(OutputSpec, TEXT("property"), OutOp, TEXT("to_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("to_node_id"), OutOp, TEXT("to_node_id"));
    CopyOptionalStringField(OutputSpec, TEXT("to_node"), OutOp, TEXT("to_node_id"));
    CopyOptionalStringField(OutputSpec, TEXT("to_pin_id"), OutOp, TEXT("to_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("to_pin"), OutOp, TEXT("to_pin_id"));
    CopyOptionalStringField(OutputSpec, TEXT("input"), OutOp, TEXT("to_pin_id"));

    const TSharedPtr<FJsonObject>* From = nullptr;
    if (OutputSpec->TryGetObjectField(TEXT("from"), From) && From != nullptr)
    {
        CopyEndpointObjectFields(*From, OutOp, TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("output"));
    }
    const TSharedPtr<FJsonObject>* To = nullptr;
    if (OutputSpec->TryGetObjectField(TEXT("to"), To) && To != nullptr)
    {
        CopyEndpointObjectFields(*To, OutOp, TEXT("to_node_id"), TEXT("to_pin_id"), TEXT("input"));
    }
    return true;
}

void AppendMaterialBuildSpecOperations(
    const TSharedPtr<FJsonObject>& Payload,
    TArray<TSharedPtr<FJsonValue>>& OutOperations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UMaterial* Material)
{
    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (Payload->HasField(TEXT("nodes")) && (!Payload->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr))
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_nodes"), TEXT("nodes must be an array"), Material);
    }
    else if (Nodes != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Nodes)
        {
            TSharedPtr<FJsonObject> NodeSpec = Value->AsObject();
            TSharedPtr<FJsonObject> Op;
            if (!NodeSpec.IsValid() || !NormalizeBuildNodeSpecToCreateOp(NodeSpec, Op))
            {
                AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_node"), TEXT("nodes entries must contain node_class/class_path and optional id/client_id"), Material);
                continue;
            }
            OutOperations.Add(MakeShared<FJsonValueObject>(Op));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
    if (Payload->HasField(TEXT("links")) && (!Payload->TryGetArrayField(TEXT("links"), Links) || Links == nullptr))
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_links"), TEXT("links must be an array"), Material);
    }
    else if (Links != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Links)
        {
            TSharedPtr<FJsonObject> LinkSpec = Value->AsObject();
            TSharedPtr<FJsonObject> Op;
            if (!LinkSpec.IsValid() || !NormalizeBuildLinkSpecToConnectOp(LinkSpec, Op))
            {
                AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_link"), TEXT("links entries must be objects"), Material);
                continue;
            }
            OutOperations.Add(MakeShared<FJsonValueObject>(Op));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    if (Payload->HasField(TEXT("material_outputs")) && (!Payload->TryGetArrayField(TEXT("material_outputs"), Outputs) || Outputs == nullptr))
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_outputs"), TEXT("material_outputs must be an array"), Material);
    }
    else if (Outputs != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Outputs)
        {
            TSharedPtr<FJsonObject> OutputSpec = Value->AsObject();
            TSharedPtr<FJsonObject> Op;
            if (!OutputSpec.IsValid() || !NormalizeBuildMaterialOutputSpecToConnectOp(OutputSpec, Op))
            {
                AddMaterialPatchDiagnostic(Diagnostics, TEXT("invalid_graph_build_output"), TEXT("material_outputs entries must be objects"), Material);
                continue;
            }
            OutOperations.Add(MakeShared<FJsonValueObject>(Op));
        }
    }
}
}
