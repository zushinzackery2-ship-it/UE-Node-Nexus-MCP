#include "UeNodeNexusBridgeBuildInfo.h"
#include "NexusLifecycle.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UeNodeNexusBridge
{
static TMap<FName, FBridgeBuildIdentity> GIdentities;

void RegisterBuildIdentity(FName Module, const FBridgeBuildIdentity& Identity)
{
    GIdentities.Add(Module, Identity);
    UE_LOG(LogTemp, Display, TEXT("Nexus phase=module_identity module=%s version=%s commit=%s dirty=%d fingerprint=%s contract=%d"),
        *Module.ToString(), *Identity.Version, *Identity.SourceCommit, Identity.bSourceDirty,
        *Identity.SourceFingerprint, Identity.ContractVersion);
}

void UnregisterBuildIdentity(FName Module)
{
    GIdentities.Remove(Module);
}

static FString ModuleBuildId(const FString& ModuleFile)
{
    FString Text;
    TSharedPtr<FJsonObject> Manifest;
    const FString File = FPaths::GetPath(ModuleFile) / TEXT("UnrealEditor.modules");
    FString BuildId;
    if (FFileHelper::LoadFileToString(Text, *File)
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) && Manifest.IsValid())
    {
        Manifest->TryGetStringField(TEXT("BuildId"), BuildId);
    }
    return BuildId;
}

TSharedPtr<FJsonObject> BridgeBuildIdentities()
{
    TSharedPtr<FJsonObject> Modules = MakeShared<FJsonObject>();
    Modules->SetObjectField(TEXT("UeNodeNexusGuard"), NexusLifecycle::BuildIdentity());
    TArray<FName> Names;
    GIdentities.GetKeys(Names);
    Names.Sort(FNameLexicalLess());
    for (FName Name : Names)
    {
        const FBridgeBuildIdentity& Identity = GIdentities[Name];
        const FString ModuleFile = FModuleManager::Get().GetModuleFilename(Name);
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("version"), Identity.Version);
        Row->SetStringField(TEXT("source_commit"), Identity.SourceCommit);
        Row->SetStringField(TEXT("source_fingerprint"), Identity.SourceFingerprint);
        Row->SetBoolField(TEXT("source_dirty"), Identity.bSourceDirty);
        Row->SetBoolField(TEXT("provenance_recorded"), Identity.bRecorded);
        Row->SetNumberField(TEXT("contract_version"), Identity.ContractVersion);
        Row->SetStringField(TEXT("module_path"), ModuleFile);
        Row->SetStringField(TEXT("build_id"), ModuleBuildId(ModuleFile));
        Row->SetBoolField(TEXT("loaded"), FModuleManager::Get().IsModuleLoaded(Name));
        Modules->SetObjectField(Name.ToString(), Row);
    }
    return Modules;
}
}
