#include "UeNodeNexusBridgeTranscodeWatch.h"

#include "UeNodeNexusBridgeTranscode.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;
static FDelegateHandle GWatchHandle;
static FTSTicker::FDelegateHandle GWatchTicker;
static FString GWatchOutDir;
static TSet<TWeakObjectPtr<UPackage>> GPendingPackages;
static FCriticalSection GWatchMutex;

static void OnPackageSavedForWatch(const FString&, UPackage* Package, FObjectPostSaveContext)
{
    FScopeLock Lock(&GWatchMutex);
    if (Package && !GWatchOutDir.IsEmpty() && Package->GetName().StartsWith(TEXT("/Game/")))
    {
        GPendingPackages.Add(Package);
    }
}

static bool TickWatch(float)
{
    if (!IsInGameThread() || IsBridgeRequestActive() || IsGarbageCollecting() || GIsSavingPackage
        || (GEditor && GEditor->IsTransactionActive()))
    {
        return true;
    }
    const double Started = FPlatformTime::Seconds();
    while ((FPlatformTime::Seconds() - Started) < 0.004)
    {
        TWeakObjectPtr<UPackage> WeakPackage;
        FString OutDir;
        {
            FScopeLock Lock(&GWatchMutex);
            if (GPendingPackages.IsEmpty())
            {
                break;
            }
            auto It = GPendingPackages.CreateIterator();
            WeakPackage = *It;
            It.RemoveCurrent();
            OutDir = GWatchOutDir;
        }
        UPackage* Package = WeakPackage.Get();
        UObject* Asset = Package ? Package->FindAssetInPackage() : nullptr;
        if (!Asset)
        {
            continue;
        }
        FBridgeWorkScope Scope(TEXT("watch-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        TSharedPtr<FJsonObject> Raw = BuildRawForAsset(Asset, KindForClass(Asset->GetClass()));
        FString File;
        FString Error;
        if (Raw.IsValid() && ResolveRawFile(OutDir, Asset->GetPathName(), File, Error))
        {
            WriteJsonFile(File, Raw, Error);
        }
        if (!Error.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("Nexus request=%s phase=watch_export asset=%s error=%s"),
                *ActiveBridgeRequestId(), *Asset->GetPathName(), *Error);
        }
    }
    return true;
}

void ShutdownTranscodeWatch()
{
    UPackage::PackageSavedWithContextEvent.Remove(GWatchHandle);
    GWatchHandle.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(GWatchTicker);
    GWatchTicker.Reset();
    FScopeLock Lock(&GWatchMutex);
    GPendingPackages.Reset();
    GWatchOutDir.Reset();
}

TSharedPtr<FJsonObject> HandleTranscodeWatchSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bEnabled = false;
    Payload->TryGetBoolField(TEXT("enabled"), bEnabled);
    FString OutDir;
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    if (bEnabled && (OutDir.IsEmpty() || !IsInsideMirrorRoot(OutDir / TEXT("x"))))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("out_dir inside the registered mirror root is required"));
    }
    ShutdownTranscodeWatch();
    if (bEnabled)
    {
        {
            FScopeLock Lock(&GWatchMutex);
            GWatchOutDir = OutDir;
        }
        GWatchHandle = UPackage::PackageSavedWithContextEvent.AddStatic(&OnPackageSavedForWatch);
        GWatchTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickWatch));
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("enabled"), bEnabled);
    Data->SetStringField(TEXT("out_dir"), GWatchOutDir);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
