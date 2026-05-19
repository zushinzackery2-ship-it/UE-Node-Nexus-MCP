#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

namespace UeNodeNexusBridge
{
static TArray<FString> MaterialRootInputLines(UMaterial* Material)
{
    TArray<FString> Lines;
    int32 Index = 0;
    for (EMaterialProperty Property : MaterialOutputProperties())
    {
        FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
        FString Source = TEXT("None");
        if (Input != nullptr && Input->Expression != nullptr)
        {
            Source = FString::Printf(TEXT("%s.outpin_%02d.%s"), *MaterialNodeAlias(Material, Input->Expression), Input->OutputIndex, *MaterialOutputName(Input->Expression, Input->OutputIndex));
        }
        Lines.Add(FString::Printf(TEXT("inpin_%02d.%s < %s"), Index++, *MaterialOutputPropertyName(Property), *Source));
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

TSharedPtr<FJsonObject> BuildMaterialOutputInterfaceData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    TArray<FString> Header = {
        TEXT("Node.Name = MaterialOutput"),
        TEXT("Node.Class = MaterialOutput"),
        TEXT("Node.Id = MaterialOutput"),
        TEXT("Node.RealId = MaterialOutput"),
        TEXT("Node.Pos = 0,0")
    };
    TArray<FString> Inputs = MaterialRootInputLines(Material);
    TArray<FString> Params = { TEXT("none_nodeparam") };
    TArray<FString> Outputs = { TEXT("none_outpin") };

    FString Section = TEXT("all");
    Payload->TryGetStringField(TEXT("section"), Section);
    FString Text;
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
        bValid = AppendSelected(Text, Inputs, Payload);
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
    Data->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
    Data->SetStringField(TEXT("node_alias"), MaterialOutputNodeId());
    SetTextPayload(Data, bValid ? Text : TEXT("index_out_of_range"));
    Data->SetBoolField(TEXT("selection_ok"), bValid);
    return Data;
}
}
