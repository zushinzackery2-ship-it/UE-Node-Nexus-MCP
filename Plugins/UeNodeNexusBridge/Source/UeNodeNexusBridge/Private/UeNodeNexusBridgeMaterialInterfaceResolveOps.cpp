#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialInterfaceResolve(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
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
