#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonValue.h"
#include "FileHelpers.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeAssetPaths.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
bool ParseInputActionValueType(const FString& Text, EInputActionValueType& OutType)
{
    if (Text.IsEmpty() || Text.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Boolean;
        return true;
    }
    if (Text.Equals(TEXT("axis1d"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis1D;
        return true;
    }
    if (Text.Equals(TEXT("axis2d"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis2D;
        return true;
    }
    if (Text.Equals(TEXT("axis3d"), ESearchCase::IgnoreCase))
    {
        OutType = EInputActionValueType::Axis3D;
        return true;
    }
    return false;
}

FString InputActionValueTypeText(EInputActionValueType Type)
{
    switch (Type)
    {
    case EInputActionValueType::Axis1D: return TEXT("axis1d");
    case EInputActionValueType::Axis2D: return TEXT("axis2d");
    case EInputActionValueType::Axis3D: return TEXT("axis3d");
    default: return TEXT("bool");
    }
}

// Validates asset_path for a create op; fills OutError when the path is
// malformed or the target package already exists.
bool PrepareNewAssetPath(const TSharedPtr<FJsonObject>& Payload, const FString& Operation, const FString& RequestId, FString& OutAssetPath, FString& OutPackageName, FString& OutAssetName, TSharedPtr<FJsonObject>& OutError)
{
    if (!Payload->TryGetStringField(TEXT("asset_path"), OutAssetPath) || OutAssetPath.IsEmpty())
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
        return false;
    }
    FText Reason;
    if (!ParseAssetPath(OutAssetPath, OutPackageName, OutAssetName, Reason))
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("invalid_asset_path"), Reason.ToString());
        return false;
    }
    const FString ObjectPath = OutPackageName + TEXT(".") + OutAssetName;
    FString ExistingFilename;
    if (FindObject<UObject>(nullptr, *ObjectPath) != nullptr || FPackageName::DoesPackageExist(OutPackageName, &ExistingFilename))
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("asset_already_exists"), TEXT("Asset package already exists"));
        return false;
    }
    return true;
}

TSharedPtr<FJsonObject> MakeInputWriteData(const FString& AssetPath, const TCHAR* AssetKind, bool bDryRun, bool bApplied, bool bSaved)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_kind"), AssetKind);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApplied);
    Data->SetBoolField(TEXT("changed"), bApplied);
    Data->SetBoolField(TEXT("saved"), bSaved);
    return Data;
}

bool SaveInputAssetPackage(UObject* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    return Package != nullptr && UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
}
}

TSharedPtr<FJsonObject> HandleInputActionCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString ValueTypeText;
    Payload->TryGetStringField(TEXT("value_type"), ValueTypeText);
    EInputActionValueType ValueType = EInputActionValueType::Boolean;
    if (!ParseInputActionValueType(ValueTypeText, ValueType))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("value_type must be one of: bool, axis1d, axis2d, axis3d"));
    }

    FString AssetPath, PackageName, AssetName;
    TSharedPtr<FJsonObject> Error;
    if (!PrepareNewAssetPath(Payload, Operation, RequestId, AssetPath, PackageName, AssetName, Error))
    {
        return Error;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);

    UInputAction* Action = nullptr;
    bool bSaved = false;
    if (!bDryRun)
    {
        UPackage* Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            return MakeOperationError(Operation, RequestId, TEXT("create_failed"), TEXT("Could not create the asset package"));
        }
        Action = NewObject<UInputAction>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (Action == nullptr)
        {
            return MakeOperationError(Operation, RequestId, TEXT("create_failed"), TEXT("Could not create the InputAction asset"));
        }
        Action->ValueType = ValueType;
        FAssetRegistryModule::AssetCreated(Action);
        Package->MarkPackageDirty();
        bSaved = bSave && SaveInputAssetPackage(Action);
    }

    TSharedPtr<FJsonObject> Data = MakeInputWriteData(Action ? Action->GetPathName() : AssetPath, TEXT("input_action"), bDryRun, Action != nullptr, bSaved);
    Data->SetStringField(TEXT("value_type"), InputActionValueTypeText(ValueType));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleInputMappingContextCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath, PackageName, AssetName;
    TSharedPtr<FJsonObject> Error;
    if (!PrepareNewAssetPath(Payload, Operation, RequestId, AssetPath, PackageName, AssetName, Error))
    {
        return Error;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);

    UInputMappingContext* Context = nullptr;
    bool bSaved = false;
    if (!bDryRun)
    {
        UPackage* Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            return MakeOperationError(Operation, RequestId, TEXT("create_failed"), TEXT("Could not create the asset package"));
        }
        Context = NewObject<UInputMappingContext>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (Context == nullptr)
        {
            return MakeOperationError(Operation, RequestId, TEXT("create_failed"), TEXT("Could not create the InputMappingContext asset"));
        }
        FAssetRegistryModule::AssetCreated(Context);
        Package->MarkPackageDirty();
        bSaved = bSave && SaveInputAssetPackage(Context);
    }

    TSharedPtr<FJsonObject> Data = MakeInputWriteData(Context ? Context->GetPathName() : AssetPath, TEXT("input_mapping_context"), bDryRun, Context != nullptr, bSaved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleInputMappingContextEntryAdd(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> ErrorResponse;
    UInputMappingContext* Context = LoadAssetOrError<UInputMappingContext>(Payload, Operation, RequestId, ErrorResponse, TEXT("InputMappingContext"));
    if (Context == nullptr)
    {
        return ErrorResponse;
    }

    FString ActionPath;
    if (!Payload->TryGetStringField(TEXT("action_path"), ActionPath) || ActionPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("action_path is required"));
    }
    UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (Action == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), TEXT("action_path did not resolve to an InputAction"));
    }

    FString KeyName;
    if (!Payload->TryGetStringField(TEXT("key"), KeyName) || KeyName.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("key is required (e.g. SpaceBar, W, Gamepad_FaceButton_Bottom)"));
    }
    const FKey Key{FName(*KeyName)};
    if (!EKeys::GetKeyDetails(Key).IsValid())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_key"), FString::Printf(TEXT("Unknown input key: %s"), *KeyName));
    }

    for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
    {
        if (Mapping.Action == Action && Mapping.Key == Key)
        {
            return MakeOperationError(Operation, RequestId, TEXT("mapping_already_exists"), TEXT("The context already maps this action to this key"));
        }
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);

    bool bSaved = false;
    if (!bDryRun)
    {
        Context->Modify();
        Context->MapKey(Action, Key);
        Context->MarkPackageDirty();
        bSaved = bSave && SaveInputAssetPackage(Context);
    }

    TSharedPtr<FJsonObject> Data = MakeInputWriteData(Context->GetPathName(), TEXT("input_mapping_context"), bDryRun, !bDryRun, bSaved);
    Data->SetStringField(TEXT("action_path"), Action->GetPathName());
    Data->SetStringField(TEXT("key"), KeyName);
    Data->SetNumberField(TEXT("mapping_count"), Context->GetMappings().Num());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleInputMappingContextGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> ErrorResponse;
    UInputMappingContext* Context = LoadAssetOrError<UInputMappingContext>(Payload, Operation, RequestId, ErrorResponse, TEXT("InputMappingContext"));
    if (Context == nullptr)
    {
        return ErrorResponse;
    }

    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
    {
        TArray<TSharedPtr<FJsonValue>> Row;
        Row.Add(MakeShared<FJsonValueString>(Mapping.Action != nullptr ? Mapping.Action->GetPathName() : FString()));
        Row.Add(MakeShared<FJsonValueString>(Mapping.Key.GetFName().ToString()));
        Row.Add(MakeShared<FJsonValueNumber>(Mapping.Triggers.Num()));
        Row.Add(MakeShared<FJsonValueNumber>(Mapping.Modifiers.Num()));
        Rows.Add(MakeShared<FJsonValueArray>(Row));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Context->GetPathName());
    Data->SetStringField(TEXT("format"), TEXT("input_mapping_context_compact"));
    Data->SetArrayField(TEXT("columns"), { MakeShared<FJsonValueString>(TEXT("action_path")), MakeShared<FJsonValueString>(TEXT("key")), MakeShared<FJsonValueString>(TEXT("trigger_count")), MakeShared<FJsonValueString>(TEXT("modifier_count")) });
    Data->SetArrayField(TEXT("mappings"), Rows);
    Data->SetNumberField(TEXT("count"), Rows.Num());
    AddElapsedMs(Data, StartSeconds);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
