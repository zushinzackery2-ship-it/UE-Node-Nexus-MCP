#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
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
TSharedPtr<FJsonObject> HandleNiagaraModulesList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (!System)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("compact");
    FString UsageFilter;
    int32 EmitterFilter = INDEX_NONE;
    Payload->TryGetStringField(TEXT("format"), Format);
    Payload->TryGetStringField(TEXT("usage"), UsageFilter);
    NiagaraModuleStack::ReadIndex(Payload, TEXT("emitter_index"), EmitterFilter);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
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
                Items.Add(bCompact
                    ? NiagaraModuleStack::ModuleToRow(EmitterIndex, Output, ModuleIndex, Modules[ModuleIndex])
                    : MakeShared<FJsonValueObject>(NiagaraModuleStack::ModuleToJson(EmitterIndex, Output, ModuleIndex, Modules[ModuleIndex])));
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_modules_compact") : TEXT("full"));
    if (bCompact)
    {
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("emitter_index")),
            MakeShared<FJsonValueString>(TEXT("usage")),
            MakeShared<FJsonValueString>(TEXT("module_index")),
            MakeShared<FJsonValueString>(TEXT("function_name")),
            MakeShared<FJsonValueString>(TEXT("script_path")),
            MakeShared<FJsonValueString>(TEXT("enabled"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
