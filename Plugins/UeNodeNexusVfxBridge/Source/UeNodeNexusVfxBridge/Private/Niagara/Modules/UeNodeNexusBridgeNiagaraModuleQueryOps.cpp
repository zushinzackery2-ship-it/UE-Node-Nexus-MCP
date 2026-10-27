#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeListFormats.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"

#include "Dom/JsonValue.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara modules format: %s"), *Format)));
    return Response;
}

static TSharedPtr<FJsonObject> BuildModulesIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& CompactItems, bool bIncludeScriptPaths)
{
    TMap<FString, int32> UsageDict;
    TArray<FString> Usages;
    TMap<FString, int32> FunctionDict;
    TArray<FString> Functions;
    TMap<FString, int32> ScriptDict;
    TArray<FString> Scripts;
    TArray<FString> Rows;

    for (const TSharedPtr<FJsonValue>& Value : CompactItems)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 6)
        {
            continue;
        }

        const int32 EmitterIndex = static_cast<int32>(Row[0]->AsNumber());
        const FString Usage = Row[1]->AsString();
        const int32 ModuleIndex = static_cast<int32>(Row[2]->AsNumber());
        const FString FunctionName = Row[3]->AsString();
        const bool bEnabled = Row[5]->AsBool();

        const int32 UsageIndex = DictIndex(UsageDict, Usages, Usage);
        const int32 FunctionIndex = DictIndex(FunctionDict, Functions, FunctionName);
        if (bIncludeScriptPaths)
        {
            const FString ScriptPath = Row[4]->AsString();
            Rows.Add(FString::Printf(
                TEXT("%d:%d:%d:%d:%d:%d"),
                EmitterIndex,
                UsageIndex,
                ModuleIndex,
                FunctionIndex,
                DictIndex(ScriptDict, Scripts, ScriptPath),
                bEnabled ? 1 : 0));
        }
        else
        {
            Rows.Add(FString::Printf(
                TEXT("%d:%d:%d:%d:%d"),
                EmitterIndex,
                UsageIndex,
                ModuleIndex,
                FunctionIndex,
                bEnabled ? 1 : 0));
        }
    }

    FString Text = FString::Printf(TEXT("G:%s|niagara|modules|%d\n"), *EscapeIndexedToken(System ? System->GetPathName() : FString()), Rows.Num());
    Text += JoinDictionaryLine(TEXT("U:"), Usages);
    Text += JoinDictionaryLine(TEXT("F:"), Functions);
    if (bIncludeScriptPaths)
    {
        Text += JoinDictionaryLine(TEXT("S:"), Scripts);
    }
    Text += TEXT("M:") + FString::Join(Rows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("niagara_modules_indexed"));
    Data->SetNumberField(TEXT("count"), Rows.Num());
    Data->SetNumberField(TEXT("usage_count"), Usages.Num());
    Data->SetNumberField(TEXT("function_count"), Functions.Num());
    Data->SetBoolField(TEXT("script_paths_omitted"), !bIncludeScriptPaths);
    if (bIncludeScriptPaths)
    {
        Data->SetNumberField(TEXT("script_count"), Scripts.Num());
    }
    SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildModulesTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& CompactItems)
{
    TMap<FString, TArray<FString>> Groups;
    TArray<FString> GroupOrder;
    for (const TSharedPtr<FJsonValue>& Value : CompactItems)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 6)
        {
            continue;
        }

        const int32 EmitterIndex = static_cast<int32>(Row[0]->AsNumber());
        const FString Usage = Row[1]->AsString();
        const int32 ModuleIndex = static_cast<int32>(Row[2]->AsNumber());
        const FString FunctionName = Row[3]->AsString();
        const bool bEnabled = Row[5]->AsBool();
        const FString Key = FString::Printf(TEXT("E%d %s"), EmitterIndex, *Usage);
        if (!Groups.Contains(Key))
        {
            GroupOrder.Add(Key);
            Groups.Add(Key, TArray<FString>());
        }
        Groups.FindChecked(Key).Add(FString::Printf(TEXT("%d %s%s"), ModuleIndex, *FunctionName, bEnabled ? TEXT("") : TEXT("(off)")));
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FString& Key : GroupOrder)
    {
        Items.Add(MakeShared<FJsonValueString>(Key + TEXT(": ") + FString::Join(Groups.FindChecked(Key), TEXT(", "))));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_modules_tiny"));
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), CompactItems.Num());
    Data->SetNumberField(TEXT("group_count"), Items.Num());
    return Data;
}

TSharedPtr<FJsonObject> HandleNiagaraModulesList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (!System)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("indexed");
    FString UsageFilter;
    int32 EmitterFilter = INDEX_NONE;
    bool bIncludeScriptPaths = false;
    Payload->TryGetStringField(TEXT("format"), Format);
    Payload->TryGetStringField(TEXT("usage"), UsageFilter);
    Payload->TryGetBoolField(TEXT("include_script_paths"), bIncludeScriptPaths);
    NiagaraModuleStack::ReadIndex(Payload, TEXT("emitter_index"), EmitterFilter);
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
    if (!bFull && !bCompact && !bIndexed && !bTiny)
    {
        return MakeInvalidFormatResponse(Operation, RequestId, Format);
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> CompactItems;
    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        if (EmitterFilter != INDEX_NONE && EmitterIndex != EmitterFilter)
        {
            continue;
        }
        UNiagaraGraph* Graph = NiagaraModuleStack::ResolveEmitterGraph(&System->GetEmitterHandles()[EmitterIndex]);
        TArray<UNiagaraNodeOutput*> Outputs;
        if (Graph)
        {
            Graph->GetNodesOfClass(Outputs);
        }
        for (UNiagaraNodeOutput* Output : Outputs)
        {
            if (!UsageFilter.IsEmpty() && !NiagaraModuleStack::UsageToString(Output->GetUsage()).Equals(UsageFilter, ESearchCase::IgnoreCase))
            {
                continue;
            }
            TArray<UNiagaraNodeFunctionCall*> Modules;
            NiagaraModuleStack::GetOrderedModules(Output, Modules);
            for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
            {
                TSharedPtr<FJsonValue> Row = NiagaraModuleStack::ModuleToRow(EmitterIndex, Output, ModuleIndex, Modules[ModuleIndex]);
                if (bFull)
                {
                    Items.Add(MakeShared<FJsonValueObject>(NiagaraModuleStack::ModuleToJson(EmitterIndex, Output, ModuleIndex, Modules[ModuleIndex])));
                }
                else if (bCompact)
                {
                    Items.Add(Row);
                }
                else
                {
                    CompactItems.Add(Row);
                }
            }
        }
    }

    if (bIndexed || bTiny)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), bIndexed ? BuildModulesIndexedData(System, CompactItems, bIncludeScriptPaths) : BuildModulesTinyData(System, CompactItems));
        return Response;
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_modules_compact") : TEXT("full"));
    if (bCompact)
    {
        SetColumns(Data, { TEXT("emitter_index"), TEXT("usage"), TEXT("module_index"), TEXT("function_name"), TEXT("script_path"), TEXT("enabled") });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
