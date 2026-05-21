#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleComponentMaterialInstanceParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    UMeshComponent* Component = nullptr;
    int32 SlotIndex = INDEX_NONE;
    UMaterialInterface* MaterialInterface = ResolveComponentSlotMaterial(Payload, Component, SlotIndex);
    if (MaterialInterface == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("material_not_found"), TEXT("component_path+slot_index must resolve to a material"));
    }

    TSharedPtr<FJsonObject> Data = BuildMaterialResolveData(MaterialInterface, true);
    Data->SetStringField(TEXT("component_path"), Component->GetPathName());
    Data->SetNumberField(TEXT("slot_index"), SlotIndex);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleComponentMaterialInstanceParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    UMeshComponent* Component = nullptr;
    int32 SlotIndex = INDEX_NONE;
    UMaterialInterface* MaterialInterface = ResolveComponentSlotMaterial(Payload, Component, SlotIndex);
    if (MaterialInterface == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("material_not_found"), TEXT("component_path+slot_index must resolve to a material"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || Params == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("invalid_request"), TEXT("params must be an array"));
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bCreateDynamic = false;
    Payload->TryGetBoolField(TEXT("create_dynamic"), bCreateDynamic);
    UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(MaterialInterface);
    if (Mid == nullptr && bCreateDynamic && !bDryRun)
    {
        Mid = Component->CreateDynamicMaterialInstance(SlotIndex, MaterialInterface);
        MaterialInterface = Mid;
    }
    if (Mid == nullptr && !(bDryRun && bCreateDynamic))
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("not_dynamic_instance"), TEXT("slot material is not a dynamic material instance; pass create_dynamic=true to create one"));
    }

    int32 Planned = 0;
    int32 Changed = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        TSharedPtr<FJsonObject> Param = Value->AsObject();
        if (!Param.IsValid())
        {
            continue;
        }
        ++Planned;
        bool bApplied = false;
        if (!bDryRun)
        {
            ApplyDynamicMaterialParam(Mid, Param, bApplied);
        }
        Changed += bApplied ? 1 : 0;
    }

    if (!bDryRun)
    {
        Component->MarkRenderStateDirty();
        Component->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("component_path"), Component->GetPathName());
    Data->SetNumberField(TEXT("slot_index"), SlotIndex);
    Data->SetStringField(TEXT("material_path"), MaterialInterface->GetPathName());
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
