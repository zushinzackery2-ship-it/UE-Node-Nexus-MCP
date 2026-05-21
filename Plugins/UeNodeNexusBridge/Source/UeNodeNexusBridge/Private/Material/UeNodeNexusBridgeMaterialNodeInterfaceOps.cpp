#include "UeNodeNexusBridgeMaterialNodeInterfaceOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static TArray<FString> MaterialInputLines(UMaterial* Material, UMaterialExpression* Expression)
{
    TArray<FString> Lines;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        const FString InputName = Expression->GetInputName(It.Index).ToString();
        FString Source = TEXT("None");
        if (Input != nullptr && Input->Expression != nullptr)
        {
            Source = FString::Printf(TEXT("%s.outpin_%02d.%s"), *MaterialNodeAlias(Material, Input->Expression), Input->OutputIndex, *MaterialOutputName(Input->Expression, Input->OutputIndex));
        }
        Lines.Add(FString::Printf(TEXT("inpin_%02d.%s < %s"), It.Index, *InputName, *Source));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("none_inpin"));
    }
    return Lines;
}

static TArray<FString> MaterialParamLines(UMaterialExpression* Expression)
{
    TArray<FString> Lines;
    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& Value : BuildMaterialExpressionParams(Expression))
    {
        const TSharedPtr<FJsonObject> Param = Value->AsObject();
        FString ParamValue = Param->GetStringField(TEXT("value"));
        ParamValue = ParamValue.IsEmpty() ? TEXT("\"\"") : ParamValue;
        Lines.Add(FString::Printf(TEXT("-nodeparam_%02d.%s = %s"), Index++, *Param->GetStringField(TEXT("name")), *ParamValue));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("none_nodeparam"));
    }
    return Lines;
}

static TArray<FString> MaterialOutputLines(UMaterial* Material, UMaterialExpression* Expression)
{
    TArray<FString> Lines;
    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        TArray<FString> Targets;
        for (TObjectPtr<UMaterialExpression> OtherPtr : Material->GetExpressions())
        {
            UMaterialExpression* Other = OtherPtr.Get();
            if (Other == nullptr)
            {
                continue;
            }
            for (FExpressionInputIterator It{ Other }; It; ++It)
            {
                if (It.Input != nullptr && It.Input->Expression == Expression && It.Input->OutputIndex == OutputIndex)
                {
                    Targets.Add(FString::Printf(TEXT("%s.inpin_%02d.%s"), *MaterialNodeAlias(Material, Other), It.Index, *Other->GetInputName(It.Index).ToString()));
                }
            }
        }
        for (EMaterialProperty Property : MaterialOutputProperties())
        {
            FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
            if (Input != nullptr && Input->Expression == Expression && Input->OutputIndex == OutputIndex)
            {
                Targets.Add(FString::Printf(TEXT("MaterialOutput.inpin_%02d.%s"), MaterialOutputProperties().Find(Property), *MaterialOutputPropertyName(Property)));
            }
        }
        Lines.Add(FString::Printf(TEXT("outpin_%02d.%s > %s"), OutputIndex, *MaterialOutputName(Expression, OutputIndex), Targets.Num() == 0 ? TEXT("None") : *FString::Join(Targets, TEXT(";"))));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("none_outpin"));
    }
    return Lines;
}

static bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, int32& OutIndex)
{
    double Number = 0.0;
    if (!Payload->TryGetNumberField(TEXT("index"), Number))
    {
        return false;
    }
    OutIndex = static_cast<int32>(Number);
    return true;
}

static bool AppendSelected(FString& Text, const TArray<FString>& Lines, const TSharedPtr<FJsonObject>& Payload)
{
    int32 Index = 0;
    if (ReadIndex(Payload, Index))
    {
        if (!Lines.IsValidIndex(Index))
        {
            return false;
        }
        Text += Lines[Index] + TEXT("\n");
        return true;
    }
    for (const FString& Line : Lines)
    {
        Text += Line + TEXT("\n");
    }
    return true;
}

TSharedPtr<FJsonObject> BuildMaterialNodeInterfaceData(UMaterial* Material, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Payload, const FString& Prefix)
{
    const FString Alias = MaterialNodeAlias(Material, Expression);
    TArray<FString> Header = {
        FString::Printf(TEXT("Node.Name = %s"), *Alias),
        FString::Printf(TEXT("Node.Class = %s"), *ShortMaterialExpressionClass(Expression)),
        FString::Printf(TEXT("Node.Id = %s"), *Alias),
        FString::Printf(TEXT("Node.RealId = %s"), *MaterialExpressionNodeId(Expression)),
        FString::Printf(TEXT("Node.Pos = %d,%d"), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY)
    };
    TArray<FString> Inputs = MaterialInputLines(Material, Expression);
    TArray<FString> Params = MaterialParamLines(Expression);
    TArray<FString> Outputs = MaterialOutputLines(Material, Expression);

    FString Section = TEXT("all");
    Payload->TryGetStringField(TEXT("section"), Section);
    FString Text = Prefix;
    bool bValid = true;
    if (Section == TEXT("brief"))
    {
        bValid = AppendSelected(Text, Header, Payload);
    }
    else if (Section == TEXT("input"))
    {
        bValid = AppendSelected(Text, Inputs, Payload);
    }
    else if (Section == TEXT("param"))
    {
        bValid = AppendSelected(Text, Params, Payload);
    }
    else if (Section == TEXT("output"))
    {
        bValid = AppendSelected(Text, Outputs, Payload);
    }
    else if (Section == TEXT("links"))
    {
        bValid = AppendSelected(Text, Inputs, Payload) && AppendSelected(Text, Outputs, Payload);
    }
    else
    {
        AppendSelected(Text, Header, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Inputs, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Params, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelected(Text, Outputs, MakeShared<FJsonObject>());
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_info_text"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), Alias);
    SetTextPayload(Data, bValid ? Text : TEXT("index_out_of_range"));
    Data->SetBoolField(TEXT("selection_ok"), bValid);
    return Data;
}

TSharedPtr<FJsonObject> HandleMaterialNodeInfoGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return Response;
    }
    if (IsMaterialOutputNodeId(NodeId))
    {
        TSharedPtr<FJsonObject> Data = BuildMaterialOutputInterfaceData(Material, Payload);
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Data->GetBoolField(TEXT("selection_ok")));
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }
    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Material, NodeId);
    if (Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("node_not_found"), TEXT("Material node was not found")));
        return Response;
    }
    TSharedPtr<FJsonObject> Data = BuildMaterialNodeInterfaceData(Material, Expression, Payload, FString());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Data->GetBoolField(TEXT("selection_ok")));
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialNodePositionGet(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    FString NodeId;
    if (!Payload->TryGetStringField(TEXT("node_id"), NodeId))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("node_id is required")));
        return Response;
    }
    UMaterialExpression* Expression = ResolveMaterialInterfaceNode(Material, NodeId);
    if (Expression == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("node_not_found"), TEXT("Material node was not found")));
        return Response;
    }

    const FString Alias = MaterialNodeAlias(Material, Expression);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_position_text"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialExpressionNodeId(Expression));
    Data->SetStringField(TEXT("node_alias"), Alias);
    SetTextPayload(Data, FString::Printf(TEXT("Node.Name = %s\nNode.Id = %s\nNode.RealId = %s\nNode.Pos = %d,%d\n"), *Alias, *Alias, *MaterialExpressionNodeId(Expression), Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
