#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialInterfaceResolve(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    // Distinguish "no resolvable target given" from "target given but unresolvable":
    // an empty call is a caller-side request error, not a missing material.
    const auto HasStringField = [&Payload](const TCHAR* Name)
    {
        FString Value;
        return Payload->TryGetStringField(Name, Value) && !Value.IsEmpty();
    };
    if (!HasStringField(TEXT("asset_path")) && !HasStringField(TEXT("material_path")) && !HasStringField(TEXT("component_path")))
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("invalid_request"), TEXT("provide one of asset_path, material_path, or component_path (with slot_index)"));
    }

    UMeshComponent* Component = nullptr;
    int32 SlotIndex = INDEX_NONE;
    UMaterialInterface* MaterialInterface = ResolveComponentSlotMaterial(Payload, Component, SlotIndex);
    if (MaterialInterface == nullptr)
    {
        MaterialInterface = ResolveMaterialInterfaceFromPayload(Payload);
    }
    if (MaterialInterface == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("material_not_found"), TEXT("Material interface could not be resolved"));
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
