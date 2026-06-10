#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static UObject* LoadRequiredAsset(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (Asset == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Asset could not be loaded")));
        return nullptr;
    }

    return Asset;
}

static TSharedPtr<FJsonObject> MakeCompileData(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount)
{
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetBoolField(TEXT("requested"), bRequested);
    Compile->SetBoolField(TEXT("ran"), bRan);
    Compile->SetBoolField(TEXT("ok"), bOk);
    Compile->SetNumberField(TEXT("error_count"), ErrorCount);
    Compile->SetNumberField(TEXT("warning_count"), WarningCount);
    return Compile;
}

TSharedPtr<FJsonObject> HandleAssetCompile(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadRequiredAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }

    FString AssetPath;
    Payload->TryGetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetPathName());

    FBridgeAssetCompileDiagnostics CompileDiagnostics = CollectAssetCompileDiagnostics(Asset, AssetPath, true);
    if (CompileDiagnostics.bSupported)
    {
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, CompileDiagnostics.bRan, CompileDiagnostics.bOk, CompileDiagnostics.ErrorCount, CompileDiagnostics.WarningCount));
        Response->SetArrayField(TEXT("diagnostics"), CompileDiagnostics.Diagnostics);
        Response->SetBoolField(TEXT("ok"), CompileDiagnostics.bOk);
    }
    else
    {
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("Only Blueprint, Material, and MaterialFunction assets support compile")));
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, false, false, 0, 0));
    }

    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleAssetValidate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetCompile(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetSave(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadRequiredAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }

    bool bOnlyIfDirty = true;
    Payload->TryGetBoolField(TEXT("only_if_dirty"), bOnlyIfDirty);

    UPackage* Package = Asset->GetOutermost();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    const bool bSaved = Package != nullptr && UEditorLoadingAndSavingUtils::SavePackages({ Package }, bOnlyIfDirty);

    TSharedPtr<FJsonObject> DirtyState = MakeShared<FJsonObject>();
    DirtyState->SetBoolField(TEXT("was_dirty"), bWasDirty);
    DirtyState->SetBoolField(TEXT("package_dirty"), Package != nullptr && Package->IsDirty());
    DirtyState->SetBoolField(TEXT("saved"), bSaved);
    DirtyState->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    DirtyState->SetStringField(TEXT("filename"), Package ? FPackageName::LongPackageNameToFilename(Package->GetName()) : FString());

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetObjectField(TEXT("dirty_state"), DirtyState);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bSaved);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bSaved)
    {
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("save_failed"), TEXT("Package save failed or package is unavailable")));
    }
    return Response;
}
}
