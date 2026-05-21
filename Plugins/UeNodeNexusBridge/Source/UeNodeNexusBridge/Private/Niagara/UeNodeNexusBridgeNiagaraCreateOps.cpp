#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeAssetCreateHelpers.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "NiagaraSystemFactoryNew.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeCreateResponseData(UNiagaraSystem* System, const FString& AssetPath, bool bDryRun, bool bSaved)
{
    TSharedPtr<FJsonObject> Data = MakeWriteData(
        bDryRun,
        !bDryRun && System != nullptr,
        !bDryRun && System != nullptr,
        MakeEmptyDiff(),
        MakePinIntegrity(true, {}, {}),
        MakeCompilePostCheck(false, false, true, 0, 0),
        MakeDirtyState(System));

    Data->SetStringField(TEXT("asset_path"), System ? System->GetPathName() : AssetPath);
    Data->SetStringField(TEXT("asset_kind"), TEXT("niagara_system"));
    Data->SetStringField(TEXT("asset_class"), System ? System->GetClass()->GetPathName() : FString());
    const TSharedPtr<FJsonObject>* PostChecks = nullptr;
    if (Data->TryGetObjectField(TEXT("post_checks"), PostChecks) && PostChecks != nullptr)
    {
        const TSharedPtr<FJsonObject>* DirtyState = nullptr;
        if ((*PostChecks)->TryGetObjectField(TEXT("dirty_state"), DirtyState) && DirtyState != nullptr)
        {
            (*DirtyState)->SetBoolField(TEXT("saved"), bSaved);
        }
    }
    return Data;
}

static TSharedPtr<FJsonObject> CreateNiagaraSystemAsset(
    const FString& Operation,
    const FString& RequestId,
    const FString& AssetPath,
    const FString& TemplateAssetPath,
    bool bCreateDefaultNodes,
    bool bDryRun,
    bool bSave)
{
    FString PackageName;
    FString AssetName;
    FText Reason;
    if (!ParseAssetPath(AssetPath, PackageName, AssetName, Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_asset_path"), Reason.ToString()));
        return Response;
    }

    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    FString PackageFilename;
    if (FindObject<UObject>(nullptr, *ObjectPath) != nullptr || FPackageName::DoesPackageExist(PackageName, &PackageFilename))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_already_exists"), TEXT("Asset package already exists")));
        return Response;
    }

    UNiagaraSystem* Template = nullptr;
    if (!TemplateAssetPath.IsEmpty())
    {
        Template = LoadObject<UNiagaraSystem>(nullptr, *TemplateAssetPath);
        if (Template == nullptr)
        {
            TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
            Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("template_not_found"), TEXT("Template Niagara system could not be loaded")));
            return Response;
        }
    }

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), MakeCreateResponseData(nullptr, ObjectPath, true, false));
        return Response;
    }

    UPackage* Package = CreatePackage(*PackageName);
    UNiagaraSystem* System = Template != nullptr
        ? Cast<UNiagaraSystem>(StaticDuplicateObject(Template, Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional))
        : NewObject<UNiagaraSystem>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
    if (System == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_create_failed"), TEXT("Niagara system factory failed")));
        return Response;
    }

    if (Template == nullptr)
    {
        UNiagaraSystemFactoryNew::InitializeSystem(System, bCreateDefaultNodes);
    }
    FAssetRegistryModule::AssetCreated(System);
    System->MarkPackageDirty();
    System->PostEditChange();

    const bool bSaved = bSave && SaveAssetPackage(System);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, !bSave || bSaved);
    Response->SetObjectField(TEXT("data"), MakeCreateResponseData(System, ObjectPath, false, bSaved));
    if (bSave && !bSaved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_failed"), TEXT("Niagara system was created but package save failed")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraSystemCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    FString TemplateAssetPath;
    bool bCreateDefaultNodes = true;
    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetStringField(TEXT("template_asset_path"), TemplateAssetPath);
    Payload->TryGetBoolField(TEXT("create_default_nodes"), bCreateDefaultNodes);
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    return CreateNiagaraSystemAsset(Operation, RequestId, AssetPath, TemplateAssetPath, bCreateDefaultNodes, bDryRun, bSave);
}

TSharedPtr<FJsonObject> HandleNiagaraTemplateDuplicate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString SourceAssetPath;
    FString DestinationAssetPath;
    if (!Payload->TryGetStringField(TEXT("source_asset_path"), SourceAssetPath) || !Payload->TryGetStringField(TEXT("destination_asset_path"), DestinationAssetPath))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("source_asset_path and destination_asset_path are required")));
        return Response;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    return CreateNiagaraSystemAsset(Operation, RequestId, DestinationAssetPath, SourceAssetPath, true, bDryRun, bSave);
}
}
