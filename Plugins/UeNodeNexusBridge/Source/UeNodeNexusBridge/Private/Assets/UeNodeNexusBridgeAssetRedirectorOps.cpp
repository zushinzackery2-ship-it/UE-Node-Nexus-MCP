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
    TArray<TSharedPtr<FJsonValue>> Redirectors;
    const int32 Count = FixRedirectorsUnderFolder(FolderPath, bDryRun, Redirectors);

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("folder_path"), FolderPath);
    Item->SetNumberField(TEXT("count"), Count);
    Item->SetArrayField(TEXT("redirectors"), Redirectors);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, !bDryRun, Count > 0, MakeAssetOpDiff(TEXT("redirectors_fixed"), Item), FolderPath, false, !bDryRun));
    return Response;
}
}
