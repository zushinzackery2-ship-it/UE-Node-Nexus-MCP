#include "UeNodeNexusBridgeOperations.h"

#include "Engine/World.h"
#include "FileHelpers.h"
#include "UeNodeNexusBridgeJson.h"
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

TSharedPtr<FJsonObject> HandleEditorSaveAll(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const bool bSaveMapPackages = ReadEditorBoolField(Payload, TEXT("save_map_packages"), true);
    const bool bSaveContentPackages = ReadEditorBoolField(Payload, TEXT("save_content_packages"), true);
    const TArray<TSharedPtr<FJsonValue>> DirtyBefore = CollectDirtyPackageNames();
    const bool bShouldSaveMaps = bSaveMapPackages && HasDirtyGameMapPackage();
    const bool bSaveAttemptSucceeded = DirtyBefore.Num() == 0 || UEditorLoadingAndSavingUtils::SaveDirtyPackages(bShouldSaveMaps, bSaveContentPackages);
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
    Data->SetArrayField(TEXT("non_game_dirty_skipped"), CollectNonGameDirtyPackageNames());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bPersistentClean);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Response->GetBoolField(TEXT("ok")))
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_dirty_packages_failed"), TEXT("One or more dirty packages could not be saved")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleEditorRequestExit(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const bool bSaveBeforeExit = ReadEditorBoolField(Payload, TEXT("save_before_exit"), true);
    const bool bForce = ReadEditorBoolField(Payload, TEXT("force"), false);
    TSharedPtr<FJsonObject> SaveData = MakeShared<FJsonObject>();
    bool bCanExit = true;
    if (bSaveBeforeExit)
    {
        TSharedPtr<FJsonObject> SaveResponse = HandleEditorSaveAll(TEXT("editor_save_all"), RequestId, Payload);
        bCanExit = SaveResponse->GetBoolField(TEXT("ok"));
        const TSharedPtr<FJsonObject>* Data = nullptr;
        if (SaveResponse->TryGetObjectField(TEXT("data"), Data) && Data != nullptr)
        {
            SaveData = *Data;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("save_before_exit"), bSaveBeforeExit);
    Data->SetBoolField(TEXT("force"), bForce);
    Data->SetBoolField(TEXT("exit_requested"), bCanExit || bForce);
    Data->SetObjectField(TEXT("save"), SaveData);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bCanExit || bForce);
    Response->SetObjectField(TEXT("data"), Data);
    if (bCanExit || bForce)
    {
        FGenericPlatformMisc::RequestExit(bForce);
    }
    else
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("exit_blocked_by_unsaved_packages"), TEXT("Dirty packages remained after save attempt")));
    }
    return Response;
}
}
