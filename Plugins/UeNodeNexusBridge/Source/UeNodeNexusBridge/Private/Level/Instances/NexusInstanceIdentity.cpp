#include "NexusInstanceIdentity.h"

#include "NexusInstanceData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Level/Scene/NexusSceneIdentity.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Instances
{
UNexusInstanceData* IdentityData(UInstancedStaticMeshComponent* Component)
{
    return Cast<UNexusInstanceData>(Component->GetAssetUserDataOfClass(UNexusInstanceData::StaticClass()));
}

bool IsIdentityBound(UInstancedStaticMeshComponent* Component)
{
    const UNexusInstanceData* Data = IdentityData(Component);
    return Data && Component->GetOwner() && Data->ActorGuid == Component->GetOwner()->GetActorGuid()
        && Data->ComponentGuid == Scene::ComponentId(Component);
}

bool IsConstructionOwned(UActorComponent* Component)
{
    if (Component->CreationMethod == EComponentCreationMethod::UserConstructionScript)
    {
        return true;
    }
    TSet<const FProperty*> Modified;
    Component->GetUCSModifiedProperties(Modified);
    for (const FProperty* Property : Modified)
    {
        const FName Name = Property->GetFName();
        if (Name == TEXT("PerInstanceSMData") || Name == TEXT("PerInstanceSMCustomData") || Name == TEXT("NumCustomDataFloats"))
        {
            return true;
        }
    }
    return false;
}

FString ContentRevision(UInstancedStaticMeshComponent* Component)
{
    FMD5 Hash;
    for (const FInstancedStaticMeshInstanceData& Data : Component->PerInstanceSMData)
    {
        Hash.Update(reinterpret_cast<const uint8*>(&Data.Transform), sizeof(Data.Transform));
    }
    Hash.Update(reinterpret_cast<const uint8*>(&Component->NumCustomDataFloats), sizeof(int32));
    Hash.Update(reinterpret_cast<const uint8*>(Component->PerInstanceSMCustomData.GetData()),
        Component->PerInstanceSMCustomData.Num() * sizeof(float));
    uint8 Result[16];
    Hash.Final(Result);
    return BytesToHex(Result, 16);
}

bool ReadIds(UInstancedStaticMeshComponent* Component, TArray<FGuid>& Out, FString& Error, bool bRebind)
{
    Out.Reset();
    const UNexusInstanceData* Metadata = IdentityData(Component);
    const bool bBound = IsIdentityBound(Component);
    if (bBound)
    {
        TSet<FGuid> Unique(Metadata->Ids);
        const bool bValid = Metadata->bIdentityValid && Metadata->Ids.Num() == Component->GetInstanceCount()
            && Unique.Num() == Metadata->Ids.Num() && !Unique.Contains(FGuid());
        if (bValid)
        {
            Out = Metadata->Ids;
            return true;
        }
        if (!bRebind)
        {
            Error = TEXT("identity_conflict: array identity is ambiguous; pull with force=ue to adopt the current snapshot");
            return false;
        }
    }
    FString Prefix = Scene::ComponentId(Component).ToString();
    if (bBound)
    {
        Prefix += TEXT("/adopt/") + ContentRevision(Component);
    }
    Out.Reserve(Component->GetInstanceCount());
    for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
    {
        Out.Add(Scene::StableGuid(Prefix + TEXT("/") + FString::FromInt(Index)));
    }
    return true;
}

UNexusInstanceData* BindIds(UInstancedStaticMeshComponent* Component, const TArray<FGuid>& Ids)
{
    UNexusInstanceData* Metadata = IdentityData(Component);
    Component->Modify();
    if (!Metadata)
    {
        Metadata = NewObject<UNexusInstanceData>(Component, NAME_None, RF_Transactional);
        Component->AddAssetUserData(Metadata);
    }
    Metadata->Modify();
    Metadata->Ids = Ids;
    Metadata->ActorGuid = Component->GetOwner()->GetActorGuid();
    Metadata->ComponentGuid = Scene::ComponentId(Component);
    Metadata->bIdentityValid = true;
    Component->MarkPackageDirty();
    return Metadata;
}
}
