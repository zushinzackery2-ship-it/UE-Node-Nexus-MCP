#include "UeNodeNexusBridgeNiagaraListFormats.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge::NiagaraListFormats
{
static void AddMaterialColumns(TSharedPtr<FJsonObject> Data)
{
    Data->SetArrayField(TEXT("columns"), {
        MakeShared<FJsonValueString>(TEXT("emitter_index")),
        MakeShared<FJsonValueString>(TEXT("renderer_index")),
        MakeShared<FJsonValueString>(TEXT("material_index")),
        MakeShared<FJsonValueString>(TEXT("renderer_class")),
        MakeShared<FJsonValueString>(TEXT("material_path"))
    });
}

static FString ShortRendererClass(const FString& ClassName)
{
    FString Result = ClassName;
    Result.RemoveFromStart(TEXT("Niagara"));
    Result.RemoveFromEnd(TEXT("RendererProperties"));
    return Result;
}

static TSharedPtr<FJsonObject> BuildMaterialIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TMap<FString, int32> ClassDict;
    TArray<FString> Classes;
    TMap<FString, int32> MaterialDict;
    TArray<FString> Materials;
    TArray<FString> EncodedRows;

    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 5)
        {
            continue;
        }
        EncodedRows.Add(FString::Printf(
            TEXT("%d:%d:%d:%d:%d"),
            static_cast<int32>(Row[0]->AsNumber()),
            static_cast<int32>(Row[1]->AsNumber()),
            static_cast<int32>(Row[2]->AsNumber()),
            NiagaraIndexedFormat::DictIndex(ClassDict, Classes, Row[3]->AsString()),
            NiagaraIndexedFormat::DictIndex(MaterialDict, Materials, Row[4]->AsString())));
    }

    FString Text = FString::Printf(TEXT("G:%s|niagara|materials|%d\n"), *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()), EncodedRows.Num());
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("C:"), Classes);
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("M:"), Materials);
    Text += TEXT("L:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("niagara_materials_indexed"));
    Data->SetNumberField(TEXT("count"), EncodedRows.Num());
    Data->SetNumberField(TEXT("class_count"), Classes.Num());
    Data->SetNumberField(TEXT("material_count"), Materials.Num());
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildMaterialTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 5)
        {
            continue;
        }
        Items.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("E%d/R%d/M%d %s %s"),
            static_cast<int32>(Row[0]->AsNumber()),
            static_cast<int32>(Row[1]->AsNumber()),
            static_cast<int32>(Row[2]->AsNumber()),
            *ShortRendererClass(Row[3]->AsString()),
            *Row[4]->AsString())));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_materials_tiny"));
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    return Data;
}

TSharedPtr<FJsonObject> BuildMaterialsListData(UNiagaraSystem* System, const FString& Format)
{
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        const FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[EmitterIndex].GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
        for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
        {
            TSharedPtr<FJsonValue> Row = MakeNiagaraMaterialRow(EmitterIndex, RendererIndex, Renderers[RendererIndex], 0);
            if (bFull)
            {
                Items.Add(MakeShared<FJsonValueObject>(MakeNiagaraMaterialJson(EmitterIndex, RendererIndex, Renderers[RendererIndex], 0)));
            }
            else if (bCompact)
            {
                Items.Add(Row);
            }
            else
            {
                Rows.Add(Row);
            }
        }
    }

    if (bIndexed)
    {
        return BuildMaterialIndexedData(System, Rows);
    }
    if (Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase))
    {
        return BuildMaterialTinyData(System, Rows);
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_materials_compact") : TEXT("full"));
    if (bCompact)
    {
        AddMaterialColumns(Data);
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    return Data;
}
}
