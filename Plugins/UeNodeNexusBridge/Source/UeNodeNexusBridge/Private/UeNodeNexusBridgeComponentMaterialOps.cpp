#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeLevelMaterialShared.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleComponentMaterialsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UMeshComponent* Component = ResolveMeshComponent(Payload, EarlyResponse, Operation, RequestId);
    if (Component == nullptr)
    {
        return EarlyResponse;
    }

    TSharedPtr<FJsonObject> Data = MeshComponentToJson(Component->GetOwner(), Component, true);
    Data->SetStringField(TEXT("format"), TEXT("component_materials_full"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleComponentMaterialsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UMeshComponent* Component = ResolveMeshComponent(Payload, EarlyResponse, Operation, RequestId);
    if (Component == nullptr)
    {
        return EarlyResponse;
    }

    int32 SlotIndex = INDEX_NONE;
    double SlotNumber = -1.0;
    if (Payload->TryGetNumberField(TEXT("slot_index"), SlotNumber))
    {
        SlotIndex = static_cast<int32>(SlotNumber);
    }
    FString MaterialPath;
    if (SlotIndex < 0 || !Payload->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        return MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("invalid_request"), TEXT("slot_index and material_path are required"));
    }
    if (SlotIndex >= Component->GetNumMaterials())
    {
        return MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("invalid_slot"), TEXT("slot_index is outside component material range"));
    }

    UMaterialInterface* Material = Cast<UMaterialInterface>(ResolveObjectByPath(MaterialPath));
    if (Material == nullptr)
    {
        return MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("material_not_found"), TEXT("Material interface could not be resolved"));
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    if (!bDryRun)
    {
        Component->Modify();
        Component->SetMaterial(SlotIndex, Material);
        Component->MarkRenderStateDirty();
        Component->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("component_path"), Component->GetPathName());
    Data->SetNumberField(TEXT("slot_index"), SlotIndex);
    Data->SetStringField(TEXT("material_path"), Material->GetPathName());
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), !bDryRun);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
