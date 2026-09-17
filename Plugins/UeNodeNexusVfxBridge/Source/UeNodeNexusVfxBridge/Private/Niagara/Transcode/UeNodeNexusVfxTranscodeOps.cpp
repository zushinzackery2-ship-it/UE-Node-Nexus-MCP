#include "UeNodeNexusVfxTranscode.h"
#include "UeNodeNexusCollaboration.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraEmitter.h"
#include "NiagaraGraph.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;
using namespace VfxTranscode;

namespace VfxTranscode
{
static bool ScriptDeprecated(UNiagaraScript* Script)
{
#if WITH_EDITORONLY_DATA
    const FVersionedNiagaraScriptData* Data = Script ? Script->GetLatestScriptData() : nullptr;
    return Data != nullptr && Data->bDeprecated;
#else
    return false;
#endif
}

static FString ScriptRecommendation(UNiagaraScript* Script)
{
#if WITH_EDITORONLY_DATA
    const FVersionedNiagaraScriptData* Data = Script ? Script->GetLatestScriptData() : nullptr;
    return Data != nullptr && Data->DeprecationRecommendation != nullptr ? Data->DeprecationRecommendation->GetPathName() : FString();
#else
    return FString();
#endif
}

TSharedPtr<FJsonObject> BuildModuleSignatures()
{
    TSharedPtr<FJsonObject> Modules = MakeShared<FJsonObject>();
    TMap<FString, TArray<FString>> PathsByShort;
    const auto AddRecord = [&Modules, &PathsByShort](const FString& Path, const TSharedPtr<FJsonObject>& Record)
    {
        Modules->SetObjectField(Path, Record);
        PathsByShort.FindOrAdd(Record->GetStringField(TEXT("short"))).Add(Path);
    };
    for (TObjectIterator<UNiagaraScript> It; It; ++It)
    {
        UNiagaraScript* Script = *It;
        // Generated per-node scripts (Set Variables) live in transient/asset-owned outers; only library modules index.
        if (Script == nullptr || Script->Usage != ENiagaraScriptUsage::Module || Script->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
            || Script->GetOutermost() == GetTransientPackage() || Script->GetOuter() != Script->GetOutermost() || Script->GetName().StartsWith(TEXT("SetVariables_")))
        {
            continue;
        }
        UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
        UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
        if (Graph == nullptr)
        {
            continue;
        }
        TArray<TSharedPtr<FJsonValue>> Inputs;
        for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : Graph->GetAllMetaData())
        {
            FString Name = Pair.Key.GetName().ToString();
            if (!Name.RemoveFromStart(TEXT("Module.")))
            {
                continue;
            }
            TSharedPtr<FJsonObject> Input = MakeShared<FJsonObject>();
            Input->SetStringField(TEXT("name"), Name);
            Input->SetStringField(TEXT("type"), FriendlyTypeName(Pair.Key.GetType()));
            Inputs.Add(MakeShared<FJsonValueObject>(Input));
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("short"), Script->GetName());
        Record->SetArrayField(TEXT("inputs"), Inputs);
        Record->SetBoolField(TEXT("deprecated"), ScriptDeprecated(Script));
        AddRecord(Script->GetPathName(), Record);
    }
    // Unloaded module scripts are indexed by name only (inputs unknown) so lint can resolve
    // them without loading the whole module library.
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    TArray<FAssetData> Assets;
    Registry.GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), Assets, true);
    for (const FAssetData& Asset : Assets)
    {
        const FString Path = Asset.GetObjectPathString();
        const FString Package = Asset.PackageName.ToString();
        if (Modules->HasField(Path) || !(Package.Contains(TEXT("/Modules/")) || Package.StartsWith(TEXT("/Game/"))))
        {
            continue;
        }
        FString Usage;
        if (Asset.GetTagValue(TEXT("Usage"), Usage) && !Usage.Contains(TEXT("Module")))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("short"), Asset.AssetName.ToString());
        Record->SetField(TEXT("inputs"), MakeShared<FJsonValueNull>());
        AddRecord(Path, Record);
    }
    // A short name that several scripts answer to must be resolved by the engine's own
    // verdict, so exactly those scripts are loaded to read their deprecation state.
    for (const TPair<FString, TArray<FString>>& Pair : PathsByShort)
    {
        if (Pair.Value.Num() < 2)
        {
            continue;
        }
        for (const FString& Path : Pair.Value)
        {
            UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *Path);
            const TSharedPtr<FJsonObject>* Record = nullptr;
            if (Script == nullptr || !Modules->TryGetObjectField(Path, Record) || Record == nullptr)
            {
                continue;
            }
            (*Record)->SetBoolField(TEXT("deprecated"), ScriptDeprecated(Script));
            (*Record)->SetStringField(TEXT("recommendation"), ScriptRecommendation(Script));
        }
    }
    return Modules;
}
}

static TSharedPtr<FJsonObject> BuildRaw(UObject* Asset)
{
    if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
    {
        return Collaboration::StampRaw(BuildNiagaraSystemRaw(System));
    }
    if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
    {
        return Collaboration::StampRaw(BuildNiagaraEmitterRaw(Emitter));
    }
    return nullptr;
}

TSharedPtr<FJsonObject> HandleVfxTranscodeExport(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString SchemaOutDir;
    if (Payload->TryGetStringField(TEXT("schema_out_dir"), SchemaOutDir) && !SchemaOutDir.IsEmpty())
    {
        FString Error;
        if (!WriteJsonFile(SchemaOutDir / TEXT("niagara_modules.json"), BuildModuleSignatures(), Error))
        {
            return MakeOperationError(Operation, RequestId, TEXT("write_failed"), Error);
        }
    }
    FString OutDir;
    const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    Payload->TryGetArrayField(TEXT("asset_paths"), Paths);
    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<TSharedPtr<FJsonValue>> Skipped;
    if (Paths != nullptr && Paths->Num() > 0)
    {
        if (OutDir.IsEmpty() || GetMirrorRoot().IsEmpty())
        {
            return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("out_dir is required and transcode_root_set must have been called"));
        }
        for (const TSharedPtr<FJsonValue>& Value : *Paths)
        {
            FString AssetPath;
            if (!Value.IsValid() || !Value->TryGetString(AssetPath))
            {
                continue;
            }
            UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
            TSharedPtr<FJsonObject> Raw = BuildRaw(Asset);
            if (!Raw.IsValid())
            {
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("asset_path"), AssetPath);
                Item->SetStringField(TEXT("reason"), Asset ? TEXT("not_niagara") : TEXT("asset_not_found"));
                Skipped.Add(MakeShared<FJsonValueObject>(Item));
                continue;
            }
            FString File;
            FString Error;
            if (!ResolveRawFile(OutDir, Asset->GetPathName(), File, Error) || !WriteJsonFile(File, Raw, Error))
            {
                return MakeOperationError(Operation, RequestId, TEXT("write_failed"), Error);
            }
            TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("asset_path"), Asset->GetPathName());
            Row->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
            Row->SetStringField(TEXT("kind"), Raw->GetStringField(TEXT("kind")));
            Row->SetStringField(TEXT("file"), File);
            Row->SetStringField(TEXT("saved_hash"), Raw->GetStringField(TEXT("saved_hash")));
            Row->SetBoolField(TEXT("dirty"), Raw->GetBoolField(TEXT("dirty")));
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("assets"), Rows);
    Data->SetArrayField(TEXT("skipped"), Skipped);
    Data->SetNumberField(TEXT("count"), Rows.Num());
    Data->SetStringField(TEXT("schema_key"), SchemaKey());
    Data->SetBoolField(TEXT("schema_written"), !SchemaOutDir.IsEmpty());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
