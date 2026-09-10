#include "NexusSceneIdentity.h"

#include "NexusSceneData.h"
#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Scene
{
FGuid StableGuid(const FString& Text)
{
    const FTCHARToUTF8 Bytes(*Text);
    FMD5 Hash;
    Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    uint8 Result[16];
    Hash.Final(Result);
    FGuid Guid;
    FGuid::ParseExact(BytesToHex(Result, 16), EGuidFormats::Digits, Guid);
    return Guid;
}

static UNexusSceneData* Metadata(AActor* Actor)
{
    return Actor ? Actor->FindComponentByClass<UNexusSceneData>() : nullptr;
}

FGuid ComponentId(UActorComponent* Component)
{
    const AActor* Actor = Component ? Component->GetOwner() : nullptr;
    if (!Actor)
    {
        return FGuid();
    }
    const FGuid ActorGuid = Actor->GetActorGuid();
    const UNexusComponentData* Data = Cast<UNexusComponentData>(Component->GetAssetUserDataOfClass(UNexusComponentData::StaticClass()));
    if (Data && Data->ActorGuid == ActorGuid && Data->Id.IsValid())
    {
        return Data->Id;
    }
    const UNexusSceneData* State = Metadata(Component->GetOwner());
    if (State && State->ActorGuid == ActorGuid && State->ComponentClasses.FindRef(Component->GetFName()) == Component->GetClass()->GetPathName())
    {
        if (const FGuid* Id = State->ComponentIds.Find(Component->GetFName()))
        {
            return *Id;
        }
    }
    return StableGuid(ActorGuid.ToString() + TEXT("/") + Component->GetName() + TEXT("/") + Component->GetClass()->GetPathName());
}

FString OwnerKey(AActor* Actor)
{
    const UNexusSceneData* State = Metadata(Actor);
    return State && State->ActorGuid == Actor->GetActorGuid() ? State->SceneKey : FString();
}

UNexusSceneData* EnsureMetadata(AActor* Actor)
{
    UNexusSceneData* State = Metadata(Actor);
    if (!State)
    {
        Actor->Modify();
        State = NewObject<UNexusSceneData>(Actor, NAME_None, RF_Transactional);
        Actor->AddInstanceComponent(State);
        State->OnComponentCreated();
        State->RegisterComponent();
    }
    if (State->ActorGuid != Actor->GetActorGuid())
    {
        State->Modify();
        State->ActorGuid = Actor->GetActorGuid();
        State->SceneKey.Reset();
        State->ComponentIds.Reset();
        State->ComponentClasses.Reset();
    }
    return State;
}

bool BindComponent(UActorComponent* Component, const FGuid& Id)
{
    UNexusSceneData* State = EnsureMetadata(Component->GetOwner());
    UNexusComponentData* Data = Cast<UNexusComponentData>(Component->GetAssetUserDataOfClass(UNexusComponentData::StaticClass()));
    if (Data && Data->Id == Id && Data->ActorGuid == State->ActorGuid
        && State->ComponentIds.FindRef(Component->GetFName()) == Id
        && State->ComponentClasses.FindRef(Component->GetFName()) == Component->GetClass()->GetPathName())
    {
        return false;
    }
    Component->Modify();
    State->Modify();
    if (!Data)
    {
        Data = NewObject<UNexusComponentData>(Component, NAME_None, RF_Transactional);
        Component->AddAssetUserData(Data);
    }
    Data->Modify();
    Data->ActorGuid = State->ActorGuid;
    Data->Id = Id;
    for (auto It = State->ComponentIds.CreateIterator(); It; ++It)
    {
        if (It.Value() == Id && It.Key() != Component->GetFName())
        {
            State->ComponentClasses.Remove(It.Key());
            It.RemoveCurrent();
        }
    }
    State->ComponentIds.Add(Component->GetFName(), Id);
    State->ComponentClasses.Add(Component->GetFName(), Component->GetClass()->GetPathName());
    Component->MarkPackageDirty();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=component_identity_bind component=%s id=%s"),
        *ActiveBridgeRequestId(), *Component->GetPathName(), *Id.ToString(EGuidFormats::Digits));
    return true;
}

void ForgetComponent(UActorComponent* Component)
{
    if (UNexusSceneData* State = Metadata(Component->GetOwner()))
    {
        State->Modify();
        State->ComponentIds.Remove(Component->GetFName());
        State->ComponentClasses.Remove(Component->GetFName());
    }
}
}
