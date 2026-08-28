#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString MapPackageNameFromPath(const FString& MapPath)
{
    FString PackageName = MapPath;
    int32 DotIndex = INDEX_NONE;
    if (PackageName.FindChar(TEXT('.'), DotIndex))
    {
        PackageName.LeftInline(DotIndex);
    }
    return PackageName;
}

bool MapPackageExists(const FString& PackageName)
{
    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*PackageName), Assets, true);
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.AssetClassPath.GetAssetName() == FName(TEXT("World")))
        {
            return true;
        }
    }
    return false;
}
}

TSharedPtr<FJsonObject> HandleLevelOpen(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString MapPath;
    if (!Payload->TryGetStringField(TEXT("map_path"), MapPath) || MapPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("map_path is required"));
    }
    const FString PackageName = MapPackageNameFromPath(MapPath);
    if (!MapPackageExists(PackageName))
    {
        return MakeOperationError(Operation, RequestId, TEXT("map_not_found"), TEXT("No World asset found for map_path"));
    }

    UWorld* CurrentWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (CurrentWorld == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("no_editor_world"), TEXT("Editor world is not available"));
    }
    UPackage* CurrentPackage = CurrentWorld->GetOutermost();
    const FString CurrentPackageName = CurrentPackage ? CurrentPackage->GetName() : FString();
    const bool bCurrentDirty = CurrentPackage ? CurrentPackage->IsDirty() : false;

    if (CurrentPackageName.Equals(PackageName, ESearchCase::IgnoreCase))
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("current_map"), CurrentPackageName);
        Data->SetBoolField(TEXT("already_open"), true);
        Data->SetBoolField(TEXT("applied"), false);
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    bool bDiscardChanges = false;
    Payload->TryGetBoolField(TEXT("discard_changes"), bDiscardChanges);
    if (bCurrentDirty && !bDiscardChanges)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("current_map"), CurrentPackageName);
        return MakeOperationError(
            Operation,
            RequestId,
            TEXT("unsaved_changes"),
            TEXT("Current map has unsaved changes; save it first or pass discard_changes=true"),
            Details);
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("previous_map"), CurrentPackageName);
    Data->SetStringField(TEXT("map_path"), PackageName);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    if (bDryRun)
    {
        Data->SetBoolField(TEXT("applied"), false);
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetMapPackageExtension()))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("map_path could not be converted to a map filename"));
    }
    FEditorFileUtils::LoadMap(Filename, false, true);

    UWorld* NewWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    UPackage* NewPackage = NewWorld ? NewWorld->GetOutermost() : nullptr;
    const FString NewPackageName = NewPackage ? NewPackage->GetName() : FString();
    const bool bLoaded = NewPackageName.Equals(PackageName, ESearchCase::IgnoreCase);
    if (!bLoaded)
    {
        return MakeOperationError(Operation, RequestId, TEXT("map_load_failed"), TEXT("Editor did not switch to the requested map"));
    }

    Data->SetStringField(TEXT("current_map"), NewPackageName);
    Data->SetBoolField(TEXT("applied"), true);
    Data->SetBoolField(TEXT("changed"), true);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
