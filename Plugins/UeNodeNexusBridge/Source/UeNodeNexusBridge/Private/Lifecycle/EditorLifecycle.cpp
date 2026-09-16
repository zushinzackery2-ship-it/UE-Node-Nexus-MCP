#include "EditorLifecycle.h"

#include "NexusLifecycle.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Transcode/Commit/NexusCommitInternal.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge::Lifecycle
{
static FTSTicker::FDelegateHandle Ticker;

static TArray<TSharedPtr<FJsonValue>> PendingRecovery()
{
    TArray<TSharedPtr<FJsonValue>> Result;
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectSavedDir() / TEXT("Nexus/Collaboration")), TEXT("receipt.json"), true, false);
    for (const FString& File : Files)
    {
        TSharedPtr<FJsonObject> Receipt;
        if (!Collaboration::ReadJournal(File, Receipt))
        {
            Result.Add(MakeShared<FJsonValueString>(File));
            continue;
        }
        const FString Phase = Collaboration::Text(Receipt, TEXT("phase"));
        if (Phase != TEXT("ue_committed") && Phase != TEXT("rolled_back") && Phase != TEXT("rejected"))
        {
            Result.Add(MakeShared<FJsonValueString>(Collaboration::Text(Receipt, TEXT("apply_id"))));
        }
    }
    return Result;
}

static bool PersistentPackage(UPackage* Package)
{
    if (GEditor && GEditor->GetEditorWorldContext().World()
        && GEditor->GetEditorWorldContext().World()->GetOutermost() == Package)
    {
        return true;
    }
    return Package != GetTransientPackage() && !Package->HasAnyFlags(RF_Transient)
        && !Package->HasAnyPackageFlags(PKG_CompiledIn)
        && !Package->GetName().StartsWith(TEXT("/Temp/"))
        && !Package->GetName().StartsWith(TEXT("/Memory/"))
        && FPackageName::IsValidLongPackageName(Package->GetName());
}

static TSharedPtr<FJsonObject> Inspect(const TArray<FString>& SavePackages, bool bAutomatic, const NexusLifecycle::FSaveGuard& CanSave)
{
    check(IsInGameThread());
    auto Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Dirty, Failures, Blockers;
    const bool bPie = GEditor && (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor);
    const bool bCompiling = FAssetCompilingManager::Get().GetNumRemainingAssets() > 0
        || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling());
    const bool bBusy = bPie || bCompiling || GIsSavingPackage || IsAsyncLoading();
    if (!bBusy)
    {
        for (const FString& Name : SavePackages)
        {
            if (!CanSave())
            {
                Blockers.Add(MakeShared<FJsonValueString>(TEXT("close_cancelled")));
                break;
            }
            UPackage* Package = FindPackage(nullptr, *Name);
            FString Error, Code;
            if (!Package || !PersistentPackage(Package)
                || (Package->IsDirty() && !Transcode::SavePackageDirect(Package, nullptr, Error, &Code)))
            {
                Failures.Add(MakeShared<FJsonValueString>(Name));
            }
        }
    }
    TArray<UPackage*> DirtyPackages;
    FEditorFileUtils::GetDirtyPackages(DirtyPackages);
    for (UPackage* Package : DirtyPackages)
    {
        Dirty.Add(MakeShared<FJsonValueString>(Package->GetName()));
    }
    const auto Recovery = PendingRecovery();
    if (!Dirty.IsEmpty())
    {
        Blockers.Add(MakeShared<FJsonValueString>(TEXT("instance_dirty")));
    }
    if (!Failures.IsEmpty())
    {
        Blockers.Add(MakeShared<FJsonValueString>(TEXT("save_failed")));
    }
    if (bBusy)
    {
        Blockers.Add(MakeShared<FJsonValueString>(bPie ? TEXT("pie_active") : TEXT("editor_busy")));
    }
    if (!Recovery.IsEmpty())
    {
        Blockers.Add(MakeShared<FJsonValueString>(TEXT("recovery_pending")));
    }
    if (bAutomatic && !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
    {
        Blockers.Add(MakeShared<FJsonValueString>(TEXT("interactive_editor")));
    }
    Result->SetArrayField(TEXT("dirty_packages"), Dirty);
    Result->SetArrayField(TEXT("failed_packages"), Failures);
    Result->SetArrayField(TEXT("recovery_pending"), Recovery);
    Result->SetArrayField(TEXT("blockers"), Blockers);
    Result->SetBoolField(TEXT("pie"), bPie);
    Result->SetBoolField(TEXT("compiling"), bCompiling);
    Result->SetBoolField(TEXT("saving"), GIsSavingPackage);
    Result->SetBoolField(TEXT("vfx_available"), FModuleManager::Get().IsModuleLoaded(TEXT("UeNodeNexusVfxBridge")));
    Result->SetStringField(TEXT("state_sampled_at"), FDateTime::UtcNow().ToIso8601());
    TSharedPtr<FJsonObject> Binding;
    if (Collaboration::ReadJournal(FPaths::ProjectSavedDir() / TEXT("Nexus/collaboration-binding.json"), Binding))
    {
        Result->SetObjectField(TEXT("collaboration_binding"), Binding);
    }
    return Result;
}

void Start()
{
    NexusLifecycle::Attach(Inspect);
    const auto Observe = []()
    {
        return true;
    };
    NexusLifecycle::Publish(Inspect(TArray<FString>(), false, Observe));
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
    {
        NexusLifecycle::Publish(Inspect(TArray<FString>(), false, []()
        {
            return true;
        }));
        return true;
    }), 10.0f);
}

void Stop()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    NexusLifecycle::Detach();
}
}
