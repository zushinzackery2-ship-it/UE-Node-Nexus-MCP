#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
// material_interface_resolve accepts EXACTLY one resolvable target:
//   - asset_path                 (a UMaterialInterface object path)
//   - material_path              (alias object path)
//   - component_path + slot_index (a mesh component material slot)
// Mixed targets are a caller error (target_conflict), not a missing material; a
// bad slot is a field error (invalid_slot), not a missing material. Only a valid,
// single target that fails to resolve yields material_not_found. Each error
// carries field-level details so the caller can correct the request precisely.
TSharedPtr<FJsonObject> HandleMaterialInterfaceResolve(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const auto HasNonEmpty = [&Payload](const TCHAR* Name)
    {
        FString Value;
        return Payload->TryGetStringField(Name, Value) && !Value.IsEmpty();
    };
    const bool bHasAsset = HasNonEmpty(TEXT("asset_path"));
    const bool bHasMaterial = HasNonEmpty(TEXT("material_path"));
    const bool bHasComponent = HasNonEmpty(TEXT("component_path"));
    const int32 TargetCount = (bHasAsset ? 1 : 0) + (bHasMaterial ? 1 : 0) + (bHasComponent ? 1 : 0);

    if (TargetCount == 0)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"),
            TEXT("provide exactly one of asset_path, material_path, or component_path (with slot_index)"));
    }
    if (TargetCount > 1)
    {
        TArray<TSharedPtr<FJsonValue>> ConflictingFields;
        if (bHasAsset) { ConflictingFields.Add(MakeShared<FJsonValueString>(TEXT("asset_path"))); }
        if (bHasMaterial) { ConflictingFields.Add(MakeShared<FJsonValueString>(TEXT("material_path"))); }
        if (bHasComponent) { ConflictingFields.Add(MakeShared<FJsonValueString>(TEXT("component_path"))); }
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetArrayField(TEXT("conflicting_fields"), ConflictingFields);
        return MakeOperationError(Operation, RequestId, TEXT("target_conflict"),
            TEXT("provide exactly one of asset_path, material_path, or component_path+slot_index"), Details);
    }

    UMeshComponent* Component = nullptr;
    int32 SlotIndex = INDEX_NONE;
    UMaterialInterface* MaterialInterface = nullptr;

    if (bHasComponent)
    {
        FString ComponentPath;
        Payload->TryGetStringField(TEXT("component_path"), ComponentPath);
        Component = Cast<UMeshComponent>(ResolveObjectByPath(ComponentPath));
        if (Component == nullptr)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("component_path"), ComponentPath);
            return MakeOperationError(Operation, RequestId, TEXT("component_not_found"),
                TEXT("component_path did not resolve to a mesh component"), Details);
        }

        double SlotNumber = 0.0;
        if (!Payload->TryGetNumberField(TEXT("slot_index"), SlotNumber))
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(TEXT("field"), TEXT("slot_index"));
            return MakeOperationError(Operation, RequestId, TEXT("invalid_request"),
                TEXT("slot_index is required with component_path"), Details);
        }

        SlotIndex = static_cast<int32>(SlotNumber);
        const int32 NumMaterials = Component->GetNumMaterials();
        if (SlotIndex < 0 || SlotIndex >= NumMaterials)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetNumberField(TEXT("slot_index"), SlotIndex);
            Details->SetNumberField(TEXT("num_materials"), NumMaterials);
            return MakeOperationError(Operation, RequestId, TEXT("invalid_slot"),
                TEXT("slot_index is outside the component material range"), Details);
        }

        MaterialInterface = Component->GetMaterial(SlotIndex);
        if (MaterialInterface == nullptr)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetNumberField(TEXT("slot_index"), SlotIndex);
            return MakeOperationError(Operation, RequestId, TEXT("material_not_found"),
                TEXT("component slot has no material assigned"), Details);
        }
    }
    else
    {
        const TCHAR* PathField = bHasAsset ? TEXT("asset_path") : TEXT("material_path");
        FString MaterialAssetPath;
        Payload->TryGetStringField(PathField, MaterialAssetPath);
        MaterialInterface = Cast<UMaterialInterface>(ResolveObjectByPath(MaterialAssetPath));
        if (MaterialInterface == nullptr)
        {
            TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
            Details->SetStringField(PathField, MaterialAssetPath);
            return MakeOperationError(Operation, RequestId, TEXT("material_not_found"),
                TEXT("Material interface could not be resolved"), Details);
        }
    }

    bool bIncludeParams = false;
    Payload->TryGetBoolField(TEXT("include_params"), bIncludeParams);
    TSharedPtr<FJsonObject> Data = BuildMaterialResolveData(MaterialInterface, bIncludeParams);
    if (Component != nullptr)
    {
        Data->SetStringField(TEXT("component_path"), Component->GetPathName());
        Data->SetNumberField(TEXT("slot_index"), SlotIndex);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
