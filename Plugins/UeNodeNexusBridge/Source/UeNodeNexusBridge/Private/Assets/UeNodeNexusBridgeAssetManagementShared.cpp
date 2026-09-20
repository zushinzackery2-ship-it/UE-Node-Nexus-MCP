#include "UeNodeNexusBridgeAssetManagementShared.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
bool ReadAssetBoolField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, bool DefaultValue)
{
    bool Value = DefaultValue;
    Payload->TryGetBoolField(FieldName, Value);
    return Value;
}

TSharedPtr<FJsonObject> MakeInvalidAssetRequest(const FString& Operation, const FString& RequestId, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), Message));
    return Response;
}

TSharedPtr<FJsonObject> MakeAssetNotFoundResponse(const FString& Operation, const FString& RequestId, const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_not_found"), AssetPath));
    return Response;
}

UObject* LoadAssetForManagement(const FString& AssetPath)
{
    if (UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath))
    {
        return Asset;
    }

    if (!AssetPath.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(AssetPath))
    {
        const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
        return LoadObject<UObject>(nullptr, *ObjectPath);
    }

    return nullptr;
}

bool SplitAssetObjectPath(const FString& ObjectPath, FString& OutPackageName, FString& OutPackagePath, FString& OutAssetName, FText& OutReason)
{
    if (ObjectPath.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidObjectPath(ObjectPath, &OutReason))
        {
            return false;
        }
        OutPackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
        OutAssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
    }
    else
    {
        if (!FPackageName::IsValidLongPackageName(ObjectPath, true, &OutReason))
        {
            return false;
        }
        OutPackageName = ObjectPath;
        OutAssetName = FPackageName::GetShortName(ObjectPath);
    }

    OutPackagePath = FPackageName::GetLongPackagePath(OutPackageName);
    return !OutPackageName.IsEmpty() && !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

TSharedPtr<FJsonObject> MakeAssetOpDiff(const FString& FieldName, const TSharedPtr<FJsonObject>& Item)
{
    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    Diff->SetArrayField(FieldName, { MakeShared<FJsonValueObject>(Item) });
    return Diff;
}

static TSharedPtr<FJsonObject> MakeSimpleDirtyState(const FString& PackageName, bool bDirty, bool bSaved)
{
    TSharedPtr<FJsonObject> DirtyState = MakeShared<FJsonObject>();
    DirtyState->SetBoolField(TEXT("package_dirty"), bDirty);
    DirtyState->SetBoolField(TEXT("saved"), bSaved);
    DirtyState->SetStringField(TEXT("package_name"), PackageName);
    return DirtyState;
}

TSharedPtr<FJsonObject> MakeAssetWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const FString& PackageName, bool bDirty, bool bSaved)
{
    return MakeWriteData(
        bDryRun,
        bApplied,
        bChanged,
        Diff,
        MakePinIntegrity(true, {}, {}),
        MakeCompilePostCheck(false, false, true, 0, 0),
        MakeSimpleDirtyState(PackageName, bDirty, bSaved));
}

TArray<TSharedPtr<FJsonValue>> PackageNamesToJson(const TArray<FName>& PackageNames)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FName& PackageName : PackageNames)
    {
        Items.Add(MakeShared<FJsonValueString>(PackageName.ToString()));
    }
    return Items;
}

void GetAssetReferencers(const FString& AssetPath, TArray<FName>& OutReferencers)
{
    FString PackageName;
    FString PackagePath;
    FString AssetName;
    FText Reason;
    if (SplitAssetObjectPath(AssetPath, PackageName, PackagePath, AssetName, Reason))
    {
        FAssetRegistryModule::GetRegistry().GetReferencers(FName(*PackageName), OutReferencers);
    }
}

}
