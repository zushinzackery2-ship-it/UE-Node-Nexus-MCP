#include "NexusSceneIdentity.h"

#include "NexusSceneApply.h"
#include "NexusSceneData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Level/Instances/NexusInstanceIdentity.h"

namespace UeNodeNexusBridge::Scene
{
namespace
{
// Instance ids of an instanced component row; false when one is not a GUID.
bool ReadInstanceIds(const FObject& ComponentRow, TArray<FGuid>& Ids)
{
    for (const auto& Instance : Rows(Object(ComponentRow, TEXT("instance_data")), TEXT("instances")))
    {
        FGuid InstanceId;
        if (!FGuid::Parse(String(Instance->AsObject(), TEXT("id")), InstanceId))
        {
            return false;
        }
        Ids.Add(InstanceId);
    }
    return true;
}

// Binds the row's component id and, unless construction owns them or they are already
// bound without adoption, its instance ids.
bool BindComponentRow(FApply& Context, AActor* Actor, const FObject& ComponentRow, bool bAdopt)
{
    UActorComponent* Component = ResolveComponent(Actor, String(ComponentRow, TEXT("name")));
    FGuid Id;
    if (!Component || !FGuid::Parse(String(ComponentRow, TEXT("id")), Id))
    {
        Context.Error = TEXT("component_missing_during_identity_binding");
        return false;
    }
    Context.bChanged |= BindComponent(Component, Id);
    UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
    if (!Instanced || Instances::IsConstructionOwned(Instanced) || (!bAdopt && Instances::IsIdentityBound(Instanced)))
    {
        return true;
    }
    TArray<FGuid> Ids;
    if (!ReadInstanceIds(ComponentRow, Ids))
    {
        Context.Error = TEXT("invalid_instance_binding");
        return false;
    }
    Instances::BindIds(Instanced, Ids);
    Context.bChanged = true;
    return true;
}
}

bool BindScene(FApply& Context)
{
    const FString Key = String(Context.Plan, TEXT("map_path")) + TEXT("#") + String(Context.Plan, TEXT("name"));
    bool bAdopt = false;
    Context.Plan->TryGetBoolField(TEXT("adopt_identities"), bAdopt);
    for (const auto& Value : Rows(Context.Before, TEXT("actors")))
    {
        const FObject Row = Value->AsObject();
        AActor* Actor = Context.Actors.FindRef(String(Row, TEXT("id")));
        if (!Actor)
        {
            Context.Error = TEXT("actor_missing_during_identity_binding");
            return false;
        }
        Context.Touch(Actor);
        UNexusSceneData* Data = EnsureMetadata(Actor);
        if (Data->SceneKey != Key)
        {
            Data->Modify();
            Data->SceneKey = Key;
            Actor->MarkPackageDirty();
            Context.bChanged = true;
        }
        for (const auto& Entry : Rows(Row, TEXT("components")))
        {
            if (!BindComponentRow(Context, Actor, Entry->AsObject(), bAdopt))
            {
                return false;
            }
        }
    }
    return true;
}

bool BindAppliedIdentities(FApply& Context)
{
    for (const auto& Pair : Context.Actors)
    {
        AActor* Actor = Pair.Value;
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
        {
            continue;
        }
        bool bChanged = false;
        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (!IsValid(Component) || Component->IsEditorOnly() || Component->HasAnyFlags(RF_Transient))
            {
                continue;
            }
            bChanged |= BindComponent(Component, ComponentId(Component));
            UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
            if (Instanced && !Instances::IsConstructionOwned(Instanced) && !Instances::IsIdentityBound(Instanced))
            {
                TArray<FGuid> Ids;
                if (!Instances::ReadIds(Instanced, Ids, Context.Error))
                {
                    return false;
                }
                Instances::BindIds(Instanced, Ids);
                bChanged = true;
            }
        }
        if (bChanged)
        {
            Context.Touch(Actor);
            Context.bChanged = true;
        }
    }
    return true;
}
}
