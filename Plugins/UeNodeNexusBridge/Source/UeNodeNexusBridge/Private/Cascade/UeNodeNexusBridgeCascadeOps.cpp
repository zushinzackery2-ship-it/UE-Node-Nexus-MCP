#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
void AddModule(TArray<TSharedPtr<FJsonValue>>& Modules, bool bCompact, const FString& EmitterName, const FString& Role, UParticleModule* Module)
{
    if (Module == nullptr)
    {
        return;
    }

    if (bCompact)
    {
        TArray<TSharedPtr<FJsonValue>> Row;
        Row.Add(MakeShared<FJsonValueString>(EmitterName));
        Row.Add(MakeShared<FJsonValueString>(Role));
        Row.Add(MakeShared<FJsonValueString>(Module->GetClass()->GetName()));
        Row.Add(MakeShared<FJsonValueString>(Module->GetPathName()));
        Modules.Add(MakeShared<FJsonValueArray>(Row));
    }
    else
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("emitter"), EmitterName);
        Json->SetStringField(TEXT("role"), Role);
        Json->SetStringField(TEXT("class"), Module->GetClass()->GetPathName());
        Json->SetStringField(TEXT("template_path"), Module->GetPathName());
        Json->SetBoolField(TEXT("enabled"), Module->bEnabled != 0);
        Modules.Add(MakeShared<FJsonValueObject>(Json));
    }
}
}

TSharedPtr<FJsonObject> HandleCascadeSystemSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UParticleSystem* System = LoadObject<UParticleSystem>(nullptr, *AssetPath);
    if (System == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Cascade ParticleSystem could not be loaded")));
        return Response;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Emitters;
    TArray<TSharedPtr<FJsonValue>> Modules;
    for (UParticleEmitter* Emitter : System->Emitters)
    {
        if (Emitter == nullptr)
        {
            continue;
        }

        const FString EmitterName = Emitter->EmitterName.ToString();
        UParticleLODLevel* Lod = Emitter->LODLevels.Num() > 0 ? Emitter->LODLevels[0] : nullptr;
        const bool bEmitterEnabled = Lod != nullptr && Lod->bEnabled != 0;
        const int32 ModuleCount = Lod != nullptr ? Lod->Modules.Num() : 0;
        const FString TypeData = (Lod != nullptr && Lod->TypeDataModule != nullptr) ? Lod->TypeDataModule->GetClass()->GetName() : TEXT("Sprite");

        if (bCompact)
        {
            TArray<TSharedPtr<FJsonValue>> Row;
            Row.Add(MakeShared<FJsonValueString>(EmitterName));
            Row.Add(MakeShared<FJsonValueBoolean>(bEmitterEnabled));
            Row.Add(MakeShared<FJsonValueString>(TypeData));
            Row.Add(MakeShared<FJsonValueNumber>(ModuleCount));
            Emitters.Add(MakeShared<FJsonValueArray>(Row));
        }
        else
        {
            TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
            Json->SetStringField(TEXT("emitter_name"), EmitterName);
            Json->SetBoolField(TEXT("enabled"), bEmitterEnabled);
            Json->SetStringField(TEXT("type_data"), TypeData);
            Json->SetNumberField(TEXT("module_count"), ModuleCount);
            Json->SetNumberField(TEXT("lod_count"), Emitter->LODLevels.Num());
            Emitters.Add(MakeShared<FJsonValueObject>(Json));
        }

        if (Lod != nullptr)
        {
            AddModule(Modules, bCompact, EmitterName, TEXT("required"), Lod->RequiredModule);
            AddModule(Modules, bCompact, EmitterName, TEXT("spawn"), Lod->SpawnModule);
            AddModule(Modules, bCompact, EmitterName, TEXT("type_data"), Lod->TypeDataModule);
            for (UParticleModule* Module : Lod->Modules)
            {
                AddModule(Modules, bCompact, EmitterName, TEXT("module"), Module);
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System->GetPathName());
    Data->SetStringField(TEXT("asset_class"), System->GetClass()->GetPathName());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("cascade_system_summary_compact"));
        TArray<TSharedPtr<FJsonValue>> EmitterColumns = { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("enabled")), MakeShared<FJsonValueString>(TEXT("type_data")), MakeShared<FJsonValueString>(TEXT("module_count")) };
        TArray<TSharedPtr<FJsonValue>> ModuleColumns = { MakeShared<FJsonValueString>(TEXT("emitter")), MakeShared<FJsonValueString>(TEXT("role")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("template_path")) };
        Data->SetArrayField(TEXT("emitter_columns"), EmitterColumns);
        Data->SetArrayField(TEXT("module_columns"), ModuleColumns);
    }
    Data->SetArrayField(TEXT("emitters"), Emitters);
    Data->SetArrayField(TEXT("modules"), Modules);
    Data->SetNumberField(TEXT("emitter_count"), Emitters.Num());
    Data->SetNumberField(TEXT("module_count"), Modules.Num());
    Data->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
