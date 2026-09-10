#include "NexusInstanceIdentity.h"

#include "NexusInstanceData.h"
#include "NexusInstanceIndices.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Level/Scene/NexusSceneIdentity.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Instances
{
static FDelegateHandle GIndexHandle;
static FDelegateHandle GPropertyHandle;
static FDelegateHandle GReplacementHandle;

static void OnIndices(UInstancedStaticMeshComponent* Component,
    TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> Updates)
{
    UNexusInstanceData* Metadata = IdentityData(Component);
    if (!IsIdentityBound(Component) || !Metadata->bIdentityValid)
    {
        return;
    }
    Metadata->Modify();
    Metadata->bIdentityValid = ApplyIndexUpdates(Metadata->Ids, Updates, Component->GetInstanceCount());
}

static void OnProperty(UObject* Object, FPropertyChangedEvent& Event)
{
    UInstancedStaticMeshComponent* Component = Cast<UInstancedStaticMeshComponent>(Object);
    const FProperty* Member = Event.MemberProperty ? Event.MemberProperty : Event.Property;
    if (!Component || !Member || Member->GetFName() != TEXT("PerInstanceSMData")
        || Event.GetArrayIndex(TEXT("PerInstanceSMData")) != INDEX_NONE || !IsIdentityBound(Component))
    {
        return;
    }
    if (Event.ChangeType == EPropertyChangeType::ValueSet || Event.ChangeType == EPropertyChangeType::Unspecified)
    {
        UNexusInstanceData* Metadata = IdentityData(Component);
        Metadata->Modify();
        Metadata->bIdentityValid = false;
        UE_LOG(LogTemp, Warning, TEXT("Nexus phase=identity_conflict component=%s reason=array_replaced"), *Component->GetPathName());
    }
}

static void OnReplaced(const TMap<UObject*, UObject*>& Replacements)
{
    for (const auto& Pair : Replacements)
    {
        UInstancedStaticMeshComponent* Before = Cast<UInstancedStaticMeshComponent>(Pair.Key);
        UInstancedStaticMeshComponent* After = Cast<UInstancedStaticMeshComponent>(Pair.Value);
        if (!Before || !After || !IsIdentityBound(Before))
        {
            continue;
        }
        const UNexusInstanceData* Previous = IdentityData(Before);
        const bool bValid = Previous->bIdentityValid && ContentRevision(Before) == ContentRevision(After);
        const TArray<FGuid> Ids = Previous->Ids;
        Scene::BindComponent(After, Scene::ComponentId(Before));
        UNexusInstanceData* Metadata = BindIds(After, Ids);
        Metadata->bIdentityValid = bValid;
    }
}

void StartupIdentity()
{
    GIndexHandle = FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.AddStatic(&OnIndices);
    GPropertyHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddStatic(&OnProperty);
    GReplacementHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddStatic(&OnReplaced);
}

void ShutdownIdentity()
{
    FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.Remove(GIndexHandle);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(GPropertyHandle);
    FCoreUObjectDelegates::OnObjectsReinstanced.Remove(GReplacementHandle);
    GIndexHandle.Reset();
    GPropertyHandle.Reset();
    GReplacementHandle.Reset();
}
}
