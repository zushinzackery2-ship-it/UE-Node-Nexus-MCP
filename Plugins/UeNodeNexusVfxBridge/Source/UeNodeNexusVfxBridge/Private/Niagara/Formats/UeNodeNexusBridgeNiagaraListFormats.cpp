#include "UeNodeNexusBridgeNiagaraListFormats.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"

#include <initializer_list>

namespace UeNodeNexusBridge::NiagaraListFormats
{
bool IsSupportedFormat(const FString& Format)
{
    return Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("full"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
}

TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara list format: %s"), *Format)));
    return Response;
}

static void AddColumns(TSharedPtr<FJsonObject> Data, std::initializer_list<const TCHAR*> Columns)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const TCHAR* Column : Columns)
    {
        Values.Add(MakeShared<FJsonValueString>(FString(Column)));
    }
    Data->SetArrayField(TEXT("columns"), Values);
}

FString ShortRendererClass(const FString& ClassName)
{
    FString Result = ClassName;
    Result.RemoveFromStart(TEXT("Niagara"));
    Result.RemoveFromEnd(TEXT("RendererProperties"));
    return Result;
}

static TSharedPtr<FJsonObject> RendererToJson(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("emitter_index"), EmitterIndex);
    Json->SetNumberField(TEXT("renderer_index"), RendererIndex);
    Json->SetStringField(TEXT("object_path"), Renderer ? Renderer->GetPathName() : FString());
    Json->SetStringField(TEXT("class"), Renderer ? Renderer->GetClass()->GetName() : FString());
    Json->SetBoolField(TEXT("enabled"), Renderer ? Renderer->GetIsEnabled() : false);
    Json->SetStringField(TEXT("material_path"), GetRendererMaterialPath(Renderer, 0));
    return Json;
}

static TSharedPtr<FJsonValue> RendererToRow(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueNumber>(EmitterIndex));
    Row.Add(MakeShared<FJsonValueNumber>(RendererIndex));
    Row.Add(MakeShared<FJsonValueString>(Renderer ? Renderer->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueBoolean>(Renderer ? Renderer->GetIsEnabled() : false));
    Row.Add(MakeShared<FJsonValueString>(GetRendererMaterialPath(Renderer, 0)));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> BuildRendererIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
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
            NiagaraIndexedFormat::DictIndex(ClassDict, Classes, Row[2]->AsString()),
            Row[3]->AsBool() ? 1 : 0,
            NiagaraIndexedFormat::DictIndex(MaterialDict, Materials, Row[4]->AsString())));
    }

    FString Text = FString::Printf(TEXT("G:%s|niagara|renderers|%d\n"), *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()), EncodedRows.Num());
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("C:"), Classes);
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("M:"), Materials);
    Text += TEXT("R:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("niagara_renderers_indexed"));
    Data->SetNumberField(TEXT("count"), EncodedRows.Num());
    Data->SetNumberField(TEXT("class_count"), Classes.Num());
    Data->SetNumberField(TEXT("material_count"), Materials.Num());
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildRendererTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
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
            TEXT("E%d/R%d %s%s %s"),
            static_cast<int32>(Row[0]->AsNumber()),
            static_cast<int32>(Row[1]->AsNumber()),
            *ShortRendererClass(Row[2]->AsString()),
            Row[3]->AsBool() ? TEXT("") : TEXT("(off)"),
            *Row[4]->AsString())));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_renderers_tiny"));
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    return Data;
}

TSharedPtr<FJsonObject> BuildRenderersListData(UNiagaraSystem* System, const FString& Format)
{
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[EmitterIndex].GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
        for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
        {
            TSharedPtr<FJsonValue> Row = RendererToRow(EmitterIndex, RendererIndex, Renderers[RendererIndex]);
            if (bFull)
            {
                Items.Add(MakeShared<FJsonValueObject>(RendererToJson(EmitterIndex, RendererIndex, Renderers[RendererIndex])));
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
        return BuildRendererIndexedData(System, Rows);
    }
    if (bTiny)
    {
        return BuildRendererTinyData(System, Rows);
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_renderers_compact") : TEXT("full"));
    if (bCompact)
    {
        AddColumns(Data, { TEXT("emitter_index"), TEXT("renderer_index"), TEXT("class"), TEXT("enabled"), TEXT("material_path") });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    return Data;
}

}
