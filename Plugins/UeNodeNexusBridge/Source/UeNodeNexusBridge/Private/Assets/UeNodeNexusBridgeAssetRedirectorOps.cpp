#include "UeNodeNexusBridgeOperations.h"

#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleAssetRedirectorsFixup(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString FolderPath = TEXT("/Game");
    Payload->TryGetStringField(TEXT("folder_path"), FolderPath);
    if (FolderPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("folder_path is required"));
    }

    FText Reason;
    if (!FPackageName::IsValidLongPackageName(FolderPath, false, &Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_folder_path"), Reason.ToString()));
        return Response;
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    FRedirectorFixupProgress Progress = FixRedirectorsUnderFolder(FolderPath, bDryRun);

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("folder_path"), FolderPath);
    Item->SetNumberField(TEXT("count"), Progress.Count);
    Item->SetArrayField(TEXT("redirectors"), Progress.Redirectors);
    Item->SetArrayField(TEXT("fixed"), Progress.DeletedRedirectors);
    Item->SetArrayField(TEXT("modified_packages"), Progress.ModifiedPackages);
    Item->SetArrayField(TEXT("saved_packages"), Progress.SavedPackages);
    Item->SetArrayField(TEXT("failed_packages"), Progress.FailedPackages);
    Item->SetArrayField(TEXT("remaining_referencers"), Progress.RemainingReferencers);
    Item->SetNumberField(TEXT("fixed_count"), Progress.DeletedRedirectors.Num());
    Item->SetNumberField(TEXT("modified_package_count"), Progress.ModifiedPackages.Num());
    Item->SetNumberField(TEXT("saved_package_count"), Progress.SavedPackages.Num());
    Item->SetBoolField(TEXT("partial"), !Progress.Error.IsEmpty() &&
        (!Progress.ModifiedPackages.IsEmpty() || !Progress.DeletedRedirectors.IsEmpty()));

    const bool bChanged = !Progress.ModifiedPackages.IsEmpty() || !Progress.DeletedRedirectors.IsEmpty();
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Progress.Error.IsEmpty());
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, !bDryRun && bChanged, bChanged,
        MakeAssetOpDiff(TEXT("redirectors_fixed"), Item), FolderPath, Progress.bDirty,
        !bDryRun && Progress.Error.IsEmpty() && Progress.SavedPackages.Num() == Progress.ModifiedPackages.Num()));
    if (!Progress.Error.IsEmpty())
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("redirector_fixup_failed"), Progress.Error));
    }
    return Response;
}
}
