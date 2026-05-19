#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> AssetDataToJson(const FAssetData& AssetData)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("object_path"), AssetData.GetObjectPathString());
    Json->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
    Json->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
    Json->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
    Json->SetStringField(TEXT("asset_class_path"), AssetData.AssetClassPath.ToString());
    Json->SetBoolField(TEXT("is_loaded"), AssetData.IsAssetLoaded());
    Json->SetBoolField(TEXT("is_redirector"), AssetData.IsRedirector());
    return Json;
}

static TSharedPtr<FJsonValue> AssetDataToRow(const FAssetData& AssetData)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AssetData.GetObjectPathString()));
    Row.Add(MakeShared<FJsonValueString>(AssetData.AssetClassPath.GetAssetName().ToString()));
    Row.Add(MakeShared<FJsonValueBoolean>(AssetData.IsAssetLoaded()));
    Row.Add(MakeShared<FJsonValueBoolean>(AssetData.IsRedirector()));
    return MakeShared<FJsonValueArray>(Row);
}

static bool ClassMatches(const FAssetData& AssetData, const TArray<FString>& ClassNames)
{
    if (ClassNames.Num() == 0)
    {
        return true;
    }

    const FString ClassPath = AssetData.AssetClassPath.ToString();
    for (const FString& ClassName : ClassNames)
    {
        if (ClassPath.Equals(ClassName, ESearchCase::IgnoreCase) || ClassPath.EndsWith(TEXT(".") + ClassName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

static bool PathMatches(const FAssetData& AssetData, const TArray<FString>& PackagePaths, bool bRecursive)
{
    if (PackagePaths.Num() == 0)
    {
        return true;
    }

    const FString AssetPackagePath = AssetData.PackagePath.ToString();
    for (const FString& PackagePath : PackagePaths)
    {
        if (bRecursive)
        {
            if (AssetPackagePath.Equals(PackagePath, ESearchCase::IgnoreCase) || AssetPackagePath.StartsWith(PackagePath + TEXT("/"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        else if (AssetPackagePath.Equals(PackagePath, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

TSharedPtr<FJsonObject> HandleAssetList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TArray<FString> ClassNames;
    Payload->TryGetStringArrayField(TEXT("class_names"), ClassNames);

    TArray<FString> PackagePaths;
    Payload->TryGetStringArrayField(TEXT("package_paths"), PackagePaths);

    bool bRecursive = true;
    Payload->TryGetBoolField(TEXT("recursive"), bRecursive);

    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 100, 1000);
    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAllAssets(Assets, true);

    TArray<TSharedPtr<FJsonValue>> Items;
    int32 MatchedIndex = 0;
    bool bHasMore = false;

    for (const FAssetData& AssetData : Assets)
    {
        if (!ClassMatches(AssetData, ClassNames) || !PathMatches(AssetData, PackagePaths, bRecursive))
        {
            continue;
        }
        if (MatchedIndex++ < Offset)
        {
            continue;
        }
        if (Items.Num() >= Limit)
        {
            bHasMore = true;
            break;
        }
        if (bCompact)
        {
            Items.Add(AssetDataToRow(AssetData));
        }
        else
        {
            Items.Add(MakeShared<FJsonValueObject>(AssetDataToJson(AssetData)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("asset_list_compact"));
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("object_path")),
            MakeShared<FJsonValueString>(TEXT("class")),
            MakeShared<FJsonValueString>(TEXT("loaded")),
            MakeShared<FJsonValueString>(TEXT("redirector"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("has_more"), bHasMore);
    if (bHasMore)
    {
        Data->SetStringField(TEXT("next_cursor"), FString::FromInt(Offset + Items.Num()));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleAssetGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAllAssets(Assets, true);
    for (const FAssetData& AssetData : Assets)
    {
        if (AssetData.GetObjectPathString().Equals(AssetPath, ESearchCase::IgnoreCase))
        {
            TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
            Response->SetObjectField(TEXT("data"), AssetDataToJson(AssetData));
            return Response;
        }
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Asset was not found in AssetRegistry")));
    return Response;
}
}
