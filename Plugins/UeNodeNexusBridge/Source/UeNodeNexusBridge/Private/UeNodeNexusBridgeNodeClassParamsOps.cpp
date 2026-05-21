#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraphNode.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeMaterialPropertySchema.h"

namespace UeNodeNexusBridge
{
static UClass* ResolveBlueprintNodeClass(const FString& NodeClass)
{
    if (UClass* Direct = LoadClass<UEdGraphNode>(nullptr, *NodeClass))
    {
        return Direct->IsChildOf(UEdGraphNode::StaticClass()) ? Direct : nullptr;
    }
    const FString ShortName = NodeClass.StartsWith(TEXT("K2Node_")) ? NodeClass : TEXT("K2Node_") + NodeClass;
    UClass* K2Class = LoadClass<UEdGraphNode>(nullptr, *FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *ShortName));
    if (K2Class != nullptr)
    {
        return K2Class;
    }
    return LoadClass<UEdGraphNode>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *NodeClass));
}

static bool IsEditableBlueprintTemplateProperty(FProperty* Property)
{
    return Property != nullptr
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
}

static TSharedPtr<FJsonObject> MakeBlueprintTemplateParam(FProperty* Property, int32 Index)
{
    TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
    Param->SetNumberField(TEXT("index"), Index);
    Param->SetStringField(TEXT("name"), Property->GetName());
    Param->SetStringField(TEXT("type"), Property->GetCPPType());
    Param->SetBoolField(TEXT("editable"), true);
    return Param;
}

static TArray<TSharedPtr<FJsonValue>> BuildBlueprintClassParams(UClass* Class)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Class == nullptr)
    {
        return Params;
    }

    int32 Index = 0;
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        FProperty* Property = *It;
        if (IsEditableBlueprintTemplateProperty(Property))
        {
            Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintTemplateParam(Property, Index++)));
        }
    }
    return Params;
}

static FString BuildClassParamsText(const FString& NodeClass, const TArray<TSharedPtr<FJsonValue>>& Params)
{
    FString Text = FString::Printf(TEXT("Node.Class = %s\n"), *NodeClass);
    if (Params.Num() == 0)
    {
        Text += TEXT("none_nodeparam\n");
        return Text;
    }
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& Value : Params)
    {
        const TSharedPtr<FJsonObject> Param = Value->AsObject();
        Text += FString::Printf(TEXT("param_%02d.%s : %s\n"), Index++, *Param->GetStringField(TEXT("name")), *Param->GetStringField(TEXT("type")));
    }
    return Text;
}

TSharedPtr<FJsonObject> HandleNodeClassParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString GraphKind;
    FString NodeClass;
    if (!Payload->TryGetStringField(TEXT("graph_kind"), GraphKind) || !Payload->TryGetStringField(TEXT("node_class"), NodeClass))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("graph_kind and node_class are required")));
        return Response;
    }

    UClass* Class = nullptr;
    if (GraphKind.Equals(TEXT("material"), ESearchCase::IgnoreCase)
        || GraphKind.Equals(TEXT("material_function"), ESearchCase::IgnoreCase))
    {
        Class = ResolveMaterialExpressionClass(NodeClass);
    }
    else if (GraphKind.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase))
    {
        Class = ResolveBlueprintNodeClass(NodeClass);
    }

    if (Class == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unknown_node_class"), FString::Printf(TEXT("Node class not found: %s"), *NodeClass)));
        return Response;
    }

    const TArray<TSharedPtr<FJsonValue>> Params = GraphKind.Equals(TEXT("material"), ESearchCase::IgnoreCase)
            || GraphKind.Equals(TEXT("material_function"), ESearchCase::IgnoreCase)
        ? BuildMaterialExpressionClassParams(Class)
        : BuildBlueprintClassParams(Class);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_class_params"));
    Data->SetStringField(TEXT("graph_kind"), GraphKind);
    Data->SetStringField(TEXT("node_class"), NodeClass);
    Data->SetStringField(TEXT("resolved_class"), Class->GetPathName());
    Data->SetArrayField(TEXT("params"), Params);
    SetTextPayload(Data, BuildClassParamsText(NodeClass, Params));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
