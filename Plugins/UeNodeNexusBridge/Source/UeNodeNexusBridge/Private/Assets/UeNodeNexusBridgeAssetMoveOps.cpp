#include "UeNodeNexusBridgeOperations.h"

#include "AssetToolsModule.h"
#include "Dom/JsonValue.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> HandleAssetMoveLike(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString SourceAssetPath;
    FString DestinationAssetPath;
    if (!Payload->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath) || !Payload->TryGetStringField(TEXT("destination_asset_path"), DestinationAssetPath) || SourceAssetPath.IsEmpty() || DestinationAssetPath.IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("source_asset_path and destination_asset_path are required"));
    }

    UObject* Asset = LoadAssetForManagement(SourceAssetPath);
    if (Asset == nullptr)
    {
        return MakeAssetNotFoundResponse(Operation, RequestId, SourceAssetPath);
    }

    FString DestinationPackageName;
    FString DestinationPackagePath;
    FString DestinationAssetName;
    FText Reason;
    if (!SplitAssetObjectPath(DestinationAssetPath, DestinationPackageName, DestinationPackagePath, DestinationAssetName, Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_destination_path"), Reason.ToString()));
        return Response;
    }
    if (FPackageName::DoesPackageExist(DestinationPackageName) || LoadObject<UObject>(nullptr, *DestinationAssetPath) != nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("destination_asset_exists"), TEXT("Destination asset already exists")));
        return Response;
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bSave = ReadAssetBoolField(Payload, TEXT("save"), false);
    const bool bFixRedirectors = ReadAssetBoolField(Payload, TEXT("fix_redirectors"), true);

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("source_asset_path"), SourceAssetPath);
    Item->SetStringField(TEXT("destination_asset_path"), DestinationAssetPath);

    bool bMoved = true;
    bool bSaved = false;
    FString FixupError;
    if (!bDryRun)
    {
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        bMoved = AssetToolsModule.Get().RenameAssets({ FAssetRenameData(Asset, DestinationPackagePath, DestinationAssetName) });
        UObject* MovedAsset = bMoved ? LoadObject<UObject>(nullptr, *DestinationAssetPath) : nullptr;
        if (MovedAsset != nullptr && bSave)
        {
            FString SaveError;
            bSaved = Transcode::SavePackageDirect(MovedAsset, SaveError);
        }
        if (bMoved && bFixRedirectors)
        {
            const FRedirectorFixupProgress Progress = FixRedirectorsUnderFolder(
                FPackageName::GetLongPackagePath(FPackageName::ObjectPathToPackageName(SourceAssetPath)), false);
            FixupError = Progress.Error;
            Item->SetArrayField(TEXT("redirectors_fixed"), Progress.DeletedRedirectors);
            if (!FixupError.IsEmpty())
            {
                Item->SetStringField(TEXT("redirector_fixup_error"), FixupError);
            }
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bMoved && FixupError.IsEmpty() && (!bSave || bDryRun || bSaved));
    Response->SetObjectField(TEXT("data"), MakeAssetWriteData(bDryRun, !bDryRun && bMoved, bMoved, MakeAssetOpDiff(TEXT("assets_moved"), Item), DestinationPackageName, !bDryRun && bMoved && !bSaved, bSaved));
    if (!bMoved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_move_failed"), TEXT("Asset move or rename failed")));
    }
    else if (bSave && !bDryRun && !bSaved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_failed"), TEXT("Asset was moved but save failed")));
    }
    else if (!FixupError.IsEmpty())
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("redirector_fixup_failed"), FixupError));
    }
    return Response;
}

static TSharedPtr<FJsonObject> HandleAssetMoveBatchLike(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const TArray<TSharedPtr<FJsonValue>>* RawItems = nullptr;
    if (!Payload->TryGetArrayField(TEXT("items"), RawItems) || RawItems == nullptr || RawItems->Num() == 0)
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("items is required"));
    }

    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bSave = ReadAssetBoolField(Payload, TEXT("save"), false);
    const bool bFixRedirectors = ReadAssetBoolField(Payload, TEXT("fix_redirectors"), true);
    const bool bContinueOnError = ReadAssetBoolField(Payload, TEXT("continue_on_error"), false);

    TArray<TSharedPtr<FJsonValue>> Results;
    int32 Succeeded = 0;
    int32 Failed = 0;

    for (int32 Index = 0; Index < RawItems->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> ItemObject = (*RawItems)[Index]->AsObject();
        if (!ItemObject.IsValid())
        {
            ++Failed;

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetNumberField(TEXT("index"), Index);
            Result->SetBoolField(TEXT("ok"), false);
            Result->SetStringField(TEXT("error_code"), TEXT("invalid_item"));
            Result->SetStringField(TEXT("error_message"), TEXT("Batch item must be an object"));
            Results.Add(MakeShared<FJsonValueObject>(Result));
            if (!bContinueOnError)
            {
                break;
            }
            continue;
        }

        FString SourceAssetPath;
        FString DestinationAssetPath;
        ItemObject->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath);
        ItemObject->TryGetStringField(TEXT("destination_asset_path"), DestinationAssetPath);

        TSharedPtr<FJsonObject> ItemPayload = MakeShared<FJsonObject>();
        ItemPayload->SetStringField(TEXT("source_asset_path"), SourceAssetPath);
        ItemPayload->SetStringField(TEXT("destination_asset_path"), DestinationAssetPath);
        ItemPayload->SetBoolField(TEXT("dry_run"), bDryRun);
        ItemPayload->SetBoolField(TEXT("save"), bSave);
        ItemPayload->SetBoolField(TEXT("fix_redirectors"), bFixRedirectors);

        const TSharedPtr<FJsonObject> ItemResponse = HandleAssetMoveLike(Operation, RequestId, ItemPayload);
        const bool bOk = ItemResponse.IsValid() && ItemResponse->GetBoolField(TEXT("ok"));
        if (bOk)
        {
            ++Succeeded;
        }
        else
        {
            ++Failed;
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("index"), Index);
        Result->SetBoolField(TEXT("ok"), bOk);
        Result->SetStringField(TEXT("source_asset_path"), SourceAssetPath);
        Result->SetStringField(TEXT("destination_asset_path"), DestinationAssetPath);

        const TSharedPtr<FJsonObject>* Data = nullptr;
        if (ItemResponse.IsValid() && ItemResponse->TryGetObjectField(TEXT("data"), Data) && Data != nullptr)
        {
            Result->SetObjectField(TEXT("data"), *Data);
        }

        const TSharedPtr<FJsonObject>* Error = nullptr;
        if (ItemResponse.IsValid() && ItemResponse->TryGetObjectField(TEXT("error"), Error) && Error != nullptr)
        {
            FString Code;
            FString Message;
            (*Error)->TryGetStringField(TEXT("code"), Code);
            (*Error)->TryGetStringField(TEXT("message"), Message);
            Result->SetStringField(TEXT("error_code"), Code);
            Result->SetStringField(TEXT("error_message"), Message);
        }

        Results.Add(MakeShared<FJsonValueObject>(Result));
        if (!bOk && !bContinueOnError)
        {
            break;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("save"), bSave);
    Data->SetBoolField(TEXT("fix_redirectors"), bFixRedirectors);
    Data->SetBoolField(TEXT("continue_on_error"), bContinueOnError);
    Data->SetNumberField(TEXT("requested"), RawItems->Num());
    Data->SetNumberField(TEXT("attempted"), Results.Num());
    Data->SetNumberField(TEXT("succeeded"), Succeeded);
    Data->SetNumberField(TEXT("failed"), Failed);
    Data->SetArrayField(TEXT("items"), Results);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Failed == 0);
    Response->SetObjectField(TEXT("data"), Data);
    if (Failed > 0)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_batch_failed"), TEXT("One or more batch asset move operations failed")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleAssetMove(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetMoveLike(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetRename(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetMoveLike(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetMoveBatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetMoveBatchLike(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetRenameBatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetMoveBatchLike(Operation, RequestId, Payload);
}
}
