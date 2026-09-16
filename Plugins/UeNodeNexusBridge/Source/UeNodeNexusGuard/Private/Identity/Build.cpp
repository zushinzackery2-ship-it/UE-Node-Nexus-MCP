#include "NexusLifecycle.h"
#include "Identity.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace NexusLifecycle
{
TSharedPtr<FJsonObject> BuildIdentity()
{
    const FString File = FModuleManager::Get().GetModuleFilename(TEXT("UeNodeNexusGuard"));
    const auto Manifest = ReadObject(FPaths::GetPath(File) / TEXT("UnrealEditor.modules"));
    FString BuildId;
    if (Manifest.IsValid())
    {
        Manifest->TryGetStringField(TEXT("BuildId"), BuildId);
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("version"), TEXT(NEXUS_BUILD_VERSION));
    Result->SetStringField(TEXT("source_commit"), TEXT(NEXUS_SOURCE_COMMIT));
    Result->SetStringField(TEXT("source_fingerprint"), TEXT(NEXUS_SOURCE_FINGERPRINT));
    Result->SetBoolField(TEXT("source_dirty"), NEXUS_SOURCE_DIRTY != 0);
    Result->SetBoolField(TEXT("provenance_recorded"), NEXUS_IDENTITY_RECORDED != 0);
    Result->SetNumberField(TEXT("contract_version"), NEXUS_CONTRACT_VERSION);
    Result->SetBoolField(TEXT("loaded"), true);
    Result->SetStringField(TEXT("module_path"), File);
    Result->SetStringField(TEXT("build_id"), BuildId);
    return Result;
}
}
