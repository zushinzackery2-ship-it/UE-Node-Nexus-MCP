#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

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
        Data->SetObjectField(TEXT("compile"), CompileDiagnosticsJson(CompileDiagnostics, true));
        Response->SetArrayField(TEXT("diagnostics"), CompileDiagnostics.Diagnostics);
        Response->SetBoolField(TEXT("ok"), CompileDiagnostics.bOk);
    }
    else
    {
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("Asset class does not support compile")));
        Data->SetObjectField(TEXT("compile"), CompileDiagnosticsJson(CompileDiagnostics, true));
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
    // Direct, prompt-free save: a checked-in (read-only) file is reported, never dialogued.
    FString SaveError;
    FString SaveCode;
    const bool bSkipped = bOnlyIfDirty && !bWasDirty;
    const bool bSaved = bSkipped || Transcode::SavePackageDirect(Package, Asset, SaveError, &SaveCode);

    TSharedPtr<FJsonObject> DirtyState = MakeShared<FJsonObject>();
    DirtyState->SetBoolField(TEXT("was_dirty"), bWasDirty);
    DirtyState->SetBoolField(TEXT("package_dirty"), Package != nullptr && Package->IsDirty());
    DirtyState->SetBoolField(TEXT("saved"), bSaved && !bSkipped);
    DirtyState->SetBoolField(TEXT("skipped_clean"), bSkipped);
    DirtyState->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    DirtyState->SetStringField(TEXT("filename"), Package ? FPackageName::LongPackageNameToFilename(Package->GetName()) : FString());

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetObjectField(TEXT("dirty_state"), DirtyState);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bSaved);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bSaved)
    {
        const FString Code = SaveCode.IsEmpty() ? FString(TEXT("save_failed")) : SaveCode;
        const FString Message = SaveError.IsEmpty() ? FString(TEXT("Package save failed or package is unavailable")) : SaveError;
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, Message));
    }
    return Response;
}
}
