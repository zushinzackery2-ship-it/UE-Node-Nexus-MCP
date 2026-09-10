#include "NexusInstanceEdit.h"

#include "NexusInstanceData.h"
#include "NexusInstanceIdentity.h"
#include "AI/NavigationSystemBase.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/ScopeExit.h"
#include "StaticMeshCompiler.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Instances
{
bool Apply(UInstancedStaticMeshComponent* Component, const TArray<FInstance>& Desired, int32 CustomCount, FString& Error)
{
    TArray<FGuid> Current;
    if (!ReadIds(Component, Current, Error))
    {
        return false;
    }
    if (UStaticMesh* Mesh = Component->GetStaticMesh())
    {
        TArray<UStaticMesh*> Meshes;
        Meshes.Add(Mesh);
        FStaticMeshCompilingManager::Get().FinishCompilation(MakeArrayView(Meshes));
    }
    Component->Modify();
    FNavigationLockContext NavigationLock(Component->GetWorld());
    UNexusInstanceData* Metadata = BindIds(Component, Current);
    UHierarchicalInstancedStaticMeshComponent* Hism = Cast<UHierarchicalInstancedStaticMeshComponent>(Component);
    const bool bAutoBuild = Hism && Hism->bAutoRebuildTreeOnInstanceChanges;
    if (Hism)
    {
        Hism->bAutoRebuildTreeOnInstanceChanges = false;
    }
    ON_SCOPE_EXIT
    {
        if (Hism)
        {
            Hism->bAutoRebuildTreeOnInstanceChanges = bAutoBuild;
            Hism->BuildTreeIfOutdated(false, false);
        }
        Component->MarkRenderStateDirty();
        FNavigationSystem::UpdateComponentData(*Component);
        Component->MarkPackageDirty();
    };
    TSet<FGuid> Wanted;
    for (const FInstance& Item : Desired)
    {
        Wanted.Add(Item.Id);
    }
    TArray<int32> Removed;
    for (int32 Index = Current.Num() - 1; Index >= 0; --Index)
    {
        if (!Wanted.Contains(Current[Index]))
        {
            Removed.Add(Index);
        }
    }
    if (!Removed.IsEmpty() && !Component->RemoveInstances(Removed, true))
    {
        Error = TEXT("remove_instances_failed");
        return false;
    }
    TArray<FGuid> Remaining;
    if (!ReadIds(Component, Remaining, Error))
    {
        return false;
    }
    const bool bResizeCustom = Component->NumCustomDataFloats != CustomCount;
    if (bResizeCustom)
    {
        Component->SetNumCustomDataFloats(CustomCount);
    }
    TMap<FGuid, int32> Indices;
    for (int32 Index = 0; Index < Metadata->Ids.Num(); ++Index)
    {
        Indices.Add(Metadata->Ids[Index], Index);
    }
    TArray<FTransform> AddedTransforms;
    TArray<const FInstance*> Added;
    for (const FInstance& Item : Desired)
    {
        if (!Indices.Contains(Item.Id))
        {
            AddedTransforms.Add(Item.Transform);
            Added.Add(&Item);
        }
    }
    Component->PreAllocateInstancesMemory(Added.Num());
    if (!Added.IsEmpty())
    {
        const TArray<int32> NewIndices = Component->AddInstances(AddedTransforms, true, false, false);
        if (NewIndices.Num() != Added.Num())
        {
            Error = TEXT("add_instances_failed");
            return false;
        }
        for (int32 Index = 0; Index < Added.Num(); ++Index)
        {
            if (!Metadata->Ids.IsValidIndex(NewIndices[Index]))
            {
                Error = TEXT("identity_index_update_failed");
                return false;
            }
            Metadata->Ids[NewIndices[Index]] = Added[Index]->Id;
            Indices.Add(Added[Index]->Id, NewIndices[Index]);
        }
    }
    TMap<int32, const FInstance*> Changed;
    for (const FInstance& Item : Desired)
    {
        const int32 Index = Indices[Item.Id];
        FTransform Existing;
        Component->GetInstanceTransform(Index, Existing, false);
        if (!Existing.Equals(Item.Transform))
        {
            Changed.Add(Index, &Item);
        }
        for (int32 Channel = 0; Channel < CustomCount; ++Channel)
        {
            const int32 Offset = Index * CustomCount + Channel;
            if (bResizeCustom || Component->PerInstanceSMCustomData[Offset] != Item.CustomData[Channel])
            {
                if (!Component->SetCustomDataValue(Index, Channel, Item.CustomData[Channel], false))
                {
                    Error = TEXT("custom_data_write_failed");
                    return false;
                }
            }
        }
    }
    TArray<int32> Sorted;
    Changed.GetKeys(Sorted);
    Sorted.Sort();
    for (int32 Cursor = 0; Cursor < Sorted.Num();)
    {
        const int32 Start = Sorted[Cursor];
        TArray<FTransform> Transforms;
        do
        {
            Transforms.Add(Changed[Sorted[Cursor]]->Transform);
            ++Cursor;
        }
        while (Cursor < Sorted.Num() && Sorted[Cursor] == Start + Transforms.Num());
        if (!Component->BatchUpdateInstancesTransforms(Start, Transforms, false, false, true))
        {
            Error = TEXT("update_instances_failed");
            return false;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s component=%s phase=instances added=%d removed=%d updated=%d"),
        *ActiveBridgeRequestId(), *Component->GetPathName(), Added.Num(), Removed.Num(), Changed.Num());
    return true;
}
}
