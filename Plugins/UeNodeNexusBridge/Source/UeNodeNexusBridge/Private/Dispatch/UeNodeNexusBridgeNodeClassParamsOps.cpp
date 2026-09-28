#include "UeNodeNexusBridgeOperations.h"

#include "EdGraph/EdGraphNode.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_Variable.h"
#include "K2Node_TransitionRuleGetter.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusBridgeMaterialPropertySchema.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

namespace UeNodeNexusBridge
{
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

static TSharedPtr<FJsonObject> MakeBlueprintCreateParam(int32 Index, const FString& Name, const FString& Type, bool bRequired)
{
    TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
    Param->SetNumberField(TEXT("index"), Index);
    Param->SetStringField(TEXT("name"), Name);
    Param->SetStringField(TEXT("type"), Type);
    Param->SetBoolField(TEXT("editable"), true);
    Param->SetBoolField(TEXT("required_on_create"), bRequired);
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
    if (Class->IsChildOf(UK2Node_TransitionRuleGetter::StaticClass()))
    {
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("getter_type"),
            TEXT("CurrentState_ElapsedTime | CurrentState_GetBlendWeight | CurrentTransitionDuration"), true)));
    }
    if (Class->IsChildOf(UK2Node_EnhancedInputAction::StaticClass()))
    {
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("input_action"), TEXT("InputAction asset path"), true)));
    }
    if (Class->IsChildOf(UK2Node_Variable::StaticClass()))
    {
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("variable_name"), TEXT("FName"), true)));
    }
    if (Class == UK2Node_Event::StaticClass())
    {
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("function_name"), TEXT("FName"), true)));
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("function_owner"), TEXT("UClass path"), true)));
    }
    else if (Class == UK2Node_CustomEvent::StaticClass())
    {
        Params.Add(MakeShared<FJsonValueObject>(MakeBlueprintCreateParam(Index++, TEXT("event_name"), TEXT("FName"), false)));
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
        Class = ResolveBlueprintNodeClassForCreate(NodeClass);
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
