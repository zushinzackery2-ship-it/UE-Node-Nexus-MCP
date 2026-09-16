#include "UeNodeNexusBridgeOperations.h"

#include "Engine/World.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge
{
static TArray<TSharedPtr<FJsonValue>> CollectDirtyPackageNames()
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Package != nullptr && Package->IsDirty() && Package->GetName().StartsWith(TEXT("/Game/")))
        {
            Items.Add(MakeShared<FJsonValueString>(Package->GetName()));
        }
    }
    return Items;
}

static TArray<TSharedPtr<FJsonValue>> CollectNonGameDirtyPackageNames()
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Package != nullptr && Package->IsDirty() && !Package->GetName().StartsWith(TEXT("/Game/")))
        {
            Items.Add(MakeShared<FJsonValueString>(Package->GetName()));
        }
    }
    return Items;
}

static bool IsDirtyGameMapPackage(UPackage* Package)
{
    if (Package == nullptr || !Package->IsDirty() || !Package->GetName().StartsWith(TEXT("/Game/")))
    {
        return false;
    }
    for (TObjectIterator<UWorld> It; It; ++It)
    {
        UWorld* World = *It;
        if (World != nullptr && World->GetOutermost() == Package)
        {
            return true;
        }
    }
    return false;
}

static bool HasDirtyGameMapPackage()
{
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        if (IsDirtyGameMapPackage(*It))
        {
            return true;
        }
    }
    return false;
}

static bool ReadEditorBoolField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, bool DefaultValue)
{
    bool Value = DefaultValue;
    Payload->TryGetBoolField(FieldName, Value);
    return Value;
}

// Saves each dirty /Game/ package directly. FEditorFileUtils::SaveDirtyPackages would open
// the source-control checkout dialog; that modal has crashed the editor mid shader compile.
static TArray<TSharedPtr<FJsonValue>> SaveDirtyGamePackages(bool bSaveMaps, bool bSaveContent, bool& bOutAllSucceeded)
{
    TArray<TSharedPtr<FJsonValue>> Failures;
    TArray<UPackage*> Packages;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (Package != nullptr && Package->IsDirty() && Package->GetName().StartsWith(TEXT("/Game/")))
        {
            Packages.Add(Package);
        }
    }
    bOutAllSucceeded = true;
    for (UPackage* Package : Packages)
    {
        const bool bMap = IsDirtyGameMapPackage(Package);
        if ((bMap && !bSaveMaps) || (!bMap && !bSaveContent))
        {
            continue;
        }
        FString Error;
        FString Code;
        if (!Transcode::SavePackageDirect(Package, nullptr, Error, &Code))
        {
            bOutAllSucceeded = false;
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("package"), Package->GetName());
            Item->SetStringField(TEXT("code"), Code);
            Item->SetStringField(TEXT("message"), Error);
            Failures.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    return Failures;
}

TSharedPtr<FJsonObject> HandleEditorSaveAll(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const bool bSaveMapPackages = ReadEditorBoolField(Payload, TEXT("save_map_packages"), true);
    const bool bSaveContentPackages = ReadEditorBoolField(Payload, TEXT("save_content_packages"), true);
    const TArray<TSharedPtr<FJsonValue>> DirtyBefore = CollectDirtyPackageNames();
    const bool bShouldSaveMaps = bSaveMapPackages && HasDirtyGameMapPackage();
    bool bSaveAttemptSucceeded = true;
    const TArray<TSharedPtr<FJsonValue>> Failures = DirtyBefore.Num() == 0 ? TArray<TSharedPtr<FJsonValue>>() : SaveDirtyGamePackages(bShouldSaveMaps, bSaveContentPackages, bSaveAttemptSucceeded);
    const TArray<TSharedPtr<FJsonValue>> DirtyAfter = CollectDirtyPackageNames();
    const bool bPersistentClean = DirtyAfter.Num() == 0;

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("saved"), bPersistentClean);
    Data->SetBoolField(TEXT("save_attempt_succeeded"), bSaveAttemptSucceeded);
    Data->SetBoolField(TEXT("saved_map_packages"), bShouldSaveMaps);
    Data->SetBoolField(TEXT("saved_content_packages"), bSaveContentPackages);
    Data->SetNumberField(TEXT("dirty_before_count"), DirtyBefore.Num());
    Data->SetNumberField(TEXT("dirty_after_count"), DirtyAfter.Num());
    Data->SetArrayField(TEXT("dirty_before"), DirtyBefore);
    Data->SetArrayField(TEXT("dirty_after"), DirtyAfter);
    Data->SetArrayField(TEXT("failed"), Failures);
    Data->SetArrayField(TEXT("non_game_dirty_skipped"), CollectNonGameDirtyPackageNames());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bPersistentClean);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Response->GetBoolField(TEXT("ok")))
    {
        const bool bReadOnly = Failures.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
        {
            return Value->AsObject()->GetStringField(TEXT("code")) == TEXT("save_blocked_read_only");
        });
        const FString Code = bReadOnly ? FString(TEXT("save_blocked_read_only")) : FString(TEXT("save_dirty_packages_failed"));
        const FString Message = bReadOnly ? FString(TEXT("Some packages are read-only on disk (source control checkout required); see data.failed")) : FString(TEXT("One or more dirty packages could not be saved; see data.failed"));
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, Message));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleEditorRequestExit(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("guarded_exit_required"),
        TEXT("Use bridge_instance_close through the Broker; preview the instance and explicit save_packages first")));
    return Response;
}
}
