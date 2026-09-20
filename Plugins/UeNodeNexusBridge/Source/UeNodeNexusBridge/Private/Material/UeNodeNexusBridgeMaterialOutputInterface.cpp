#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"

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

static TArray<FString> MaterialOutputParamLines(UMaterial* Material)
{
    TArray<FString> Lines;
    for (const TSharedPtr<FJsonValue>& Value : BuildMaterialOutputParams(Material))
    {
        TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            continue;
        }

        FString Name;
        FString ParamValue;
        Object->TryGetStringField(TEXT("name"), Name);
        Object->TryGetStringField(TEXT("value"), ParamValue);
        Lines.Add(FString::Printf(TEXT("- %s=%s"), *Name, *ParamValue));
    }
    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("none_nodeparam"));
    }
    return Lines;
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
    TArray<FString> Params = MaterialOutputParamLines(Material);
    TArray<FString> Outputs = { TEXT("none_outpin") };

    FString Section = TEXT("all");
    Payload->TryGetStringField(TEXT("section"), Section);
    FString Text;
    bool bValid = true;
    if (Section == TEXT("brief"))
    {
        bValid = AppendSelectedLines(Text, Header, Payload);
    }
    else if (Section == TEXT("input"))
    {
        bValid = AppendSelectedLines(Text, Inputs, Payload);
    }
    else if (Section == TEXT("param"))
    {
        bValid = AppendSelectedLines(Text, Params, Payload);
    }
    else if (Section == TEXT("output"))
    {
        bValid = AppendSelectedLines(Text, Outputs, Payload);
    }
    else if (Section == TEXT("links"))
    {
        bValid = AppendSelectedLines(Text, Inputs, Payload);
    }
    else
    {
        AppendSelectedLines(Text, Header, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Inputs, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Params, MakeShared<FJsonObject>());
        Text += TEXT("\n");
        AppendSelectedLines(Text, Outputs, MakeShared<FJsonObject>());
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("node_info_text"));
    Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Data->SetStringField(TEXT("graph_kind"), TEXT("material"));
    Data->SetStringField(TEXT("graph_name"), TEXT("MaterialGraph"));
    Data->SetStringField(TEXT("node_id"), MaterialOutputNodeId());
    Data->SetStringField(TEXT("node_alias"), MaterialOutputNodeId());
    FString Format = TEXT("text");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (Format.Equals(TEXT("compact_json"), ESearchCase::IgnoreCase))
    {
        TArray<TSharedPtr<FJsonValue>> InputRows;
        for (EMaterialProperty Property : MaterialOutputProperties())
        {
            TArray<TSharedPtr<FJsonValue>> Cells;
            Cells.Add(MakeShared<FJsonValueString>(MaterialOutputPropertyName(Property)));
            FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
            if (Input != nullptr && Input->Expression != nullptr)
            {
                Cells.Add(MakeShared<FJsonValueString>(MaterialNodeAlias(Material, Input->Expression)));
                Cells.Add(MakeShared<FJsonValueString>(MaterialOutputName(Input->Expression, Input->OutputIndex)));
            }
            else
            {
                Cells.Add(MakeShared<FJsonValueNull>());
            }
            InputRows.Add(MakeShared<FJsonValueArray>(Cells));
        }
        Data->SetStringField(TEXT("format"), TEXT("node_info_compact_json"));
        Data->SetArrayField(TEXT("input"), InputRows);
        Data->SetArrayField(TEXT("param"), BuildCompactParamRows(BuildMaterialOutputParams(Material), TEXT("value")));
        Data->SetArrayField(TEXT("output"), {});
    }
    SetTextPayload(Data, bValid ? Text : TEXT("index_out_of_range"));
    Data->SetBoolField(TEXT("selection_ok"), bValid);
    return Data;
}
}
