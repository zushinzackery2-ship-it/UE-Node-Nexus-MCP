#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge
{
static bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue)
{
    double Number = -1.0;
    if (!Payload->TryGetNumberField(Field, Number))
    {
        OutValue = INDEX_NONE;
        return false;
    }
    OutValue = static_cast<int32>(Number);
    return true;
}

static FNiagaraEmitterHandle* ResolveEmitterHandle(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    int32 EmitterIndex = INDEX_NONE;
    if (!ReadIndex(Payload, TEXT("emitter_index"), EmitterIndex) || !System->GetEmitterHandles().IsValidIndex(EmitterIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_emitter_index"), TEXT("emitter_index is required and must point to an existing emitter")));
        return nullptr;
    }
    return &System->GetEmitterHandles()[EmitterIndex];
}

static void AddEmitterDataFields(FNiagaraEmitterHandle* Handle, TSharedPtr<FJsonObject> Data)
{
    FVersionedNiagaraEmitterData* EmitterData = Handle ? Handle->GetEmitterData() : nullptr;
    Data->SetStringField(TEXT("name"), Handle ? Handle->GetName().ToString() : FString());
    Data->SetBoolField(TEXT("enabled"), Handle ? Handle->GetIsEnabled() : false);
    Data->SetStringField(TEXT("mode"), Handle && Handle->GetEmitterMode() == ENiagaraEmitterMode::Stateless ? TEXT("stateless") : TEXT("standard"));
    if (EmitterData == nullptr)
    {
        return;
    }
    Data->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
    Data->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);
    Data->SetBoolField(TEXT("interpolated_spawning"), EmitterData->bInterpolatedSpawning != 0);
    Data->SetNumberField(TEXT("random_seed"), EmitterData->RandomSeed);
    Data->SetStringField(TEXT("sim_target"), StaticEnum<ENiagaraSimTarget>()->GetNameStringByValue(static_cast<int64>(EmitterData->SimTarget)));
    Data->SetNumberField(TEXT("renderer_count"), EmitterData->GetRenderers().Num());
}

static bool ApplyEmitterField(UNiagaraSystem* System, FNiagaraEmitterHandle* Handle, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    if (!Param->TryGetStringField(TEXT("name"), Name))
    {
        return false;
    }
    if (Name.Equals(TEXT("name"), ESearchCase::IgnoreCase))
    {
        FString Value;
        if (Param->TryGetStringField(TEXT("value"), Value) && !Value.IsEmpty())
        {
            Handle->SetName(FName(*Value), *System);
            return true;
        }
        return false;
    }
    if (Name.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
    {
        bool bValue = false;
        return Param->TryGetBoolField(TEXT("value"), bValue) && Handle->SetIsEnabled(bValue, *System, true);
    }
    FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
    if (EmitterData == nullptr)
    {
        return false;
    }
    if (Name.Equals(TEXT("local_space"), ESearchCase::IgnoreCase))
    {
        bool bValue = false;
        if (Param->TryGetBoolField(TEXT("value"), bValue))
        {
            EmitterData->bLocalSpace = bValue;
            return true;
        }
    }
    if (Name.Equals(TEXT("determinism"), ESearchCase::IgnoreCase))
    {
        bool bValue = false;
        if (Param->TryGetBoolField(TEXT("value"), bValue))
        {
            EmitterData->bDeterminism = bValue;
            return true;
        }
    }
    if (Name.Equals(TEXT("interpolated_spawning"), ESearchCase::IgnoreCase))
    {
        bool bValue = false;
        if (Param->TryGetBoolField(TEXT("value"), bValue))
        {
            EmitterData->bInterpolatedSpawning = bValue;
            return true;
        }
    }
    if (Name.Equals(TEXT("random_seed"), ESearchCase::IgnoreCase))
    {
        double Value = 0.0;
        if (Param->TryGetNumberField(TEXT("value"), Value))
        {
            EmitterData->RandomSeed = static_cast<int32>(Value);
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> HandleNiagaraEmitterPropertiesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = ResolveEmitterHandle(System, Payload, EarlyResponse, Operation, RequestId);
    if (Handle == nullptr)
    {
        return EarlyResponse;
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    AddEmitterDataFields(Handle, Data);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraEmitterPropertiesSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = ResolveEmitterHandle(System, Payload, EarlyResponse, Operation, RequestId);
    if (Handle == nullptr)
    {
        return EarlyResponse;
    }
    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("params must be an array")));
        return Response;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    int32 Planned = 0;
    int32 Changed = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        TSharedPtr<FJsonObject> Param = Value->AsObject();
        if (!Param.IsValid())
        {
            continue;
        }
        ++Planned;
        if (!bDryRun && ApplyEmitterField(System, Handle, Param))
        {
            ++Changed;
        }
    }
    if (!bDryRun && Changed > 0)
    {
        System->Modify();
        System->MarkPackageDirty();
        System->PostEditChange();
    }
    const bool bSaved = bSave && Changed > 0 && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);
    Data->SetBoolField(TEXT("saved"), bSaved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
