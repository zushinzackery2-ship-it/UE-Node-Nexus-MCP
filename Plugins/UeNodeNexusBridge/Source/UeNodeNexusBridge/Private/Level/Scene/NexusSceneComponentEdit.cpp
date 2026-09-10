#include "NexusSceneApply.h"

#include "NexusSceneProperties.h"
#include "NexusSceneIdentity.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Level/Instances/NexusInstanceEdit.h"
#include "Level/Instances/NexusInstanceIdentity.h"

namespace UeNodeNexusBridge::Scene
{
bool ApplyComponent(FApply& Context, const FObject& Op)
{
    AActor* Actor = Context.Actors.FindRef(String(Op, TEXT("actor_id")));
    if (!IsValid(Actor))
    {
        Context.Error = TEXT("component_owner_missing");
        return false;
    }
    Context.Touch(Actor);
    const FString Verb = String(Op, TEXT("op"));
    const FString Name = String(Op, TEXT("id"));
    UActorComponent* Component = ResolveComponent(Actor, Name);
    if (Verb == TEXT("create_component") && Context.CreatedActors.Contains(String(Op, TEXT("actor_id"))))
    {
        Component = ResolveComponent(Actor, String(Op, TEXT("name")));
    }
    if (Verb == TEXT("create_component") && !Component)
    {
        UClass* Type = LoadClass<UActorComponent>(nullptr, *String(Op, TEXT("class")));
        Component = NewObject<UActorComponent>(Actor, Type, FName(*String(Op, TEXT("name"))), RF_Transactional);
        Actor->Modify();
        Actor->AddInstanceComponent(Component);
        Component->OnComponentCreated();
        if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
        {
            if (!Actor->GetRootComponent())
            {
                Actor->SetRootComponent(SceneComponent);
            }
            else
            {
                SceneComponent->SetupAttachment(Actor->GetRootComponent());
            }
        }
        Component->RegisterComponent();
    }
    if (!IsValid(Component))
    {
        Context.Error = TEXT("component_missing_during_apply: ") + Name;
        return false;
    }
    Component->Modify();
    Context.bChanged = true;
    if (Verb == TEXT("create_component"))
    {
        FGuid Guid;
        FGuid::Parse(Name, Guid);
        BindComponent(Component, Guid);
        return true;
    }
    if (Verb == TEXT("remove_component"))
    {
        Actor->Modify();
        ForgetComponent(Component);
        Actor->RemoveInstanceComponent(Component);
        Component->DestroyComponent();
        return true;
    }
    if (Verb == TEXT("instances_set"))
    {
        UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
        if (!Instanced || Instances::IsConstructionOwned(Instanced))
        {
            Context.Error = TEXT("construction_instances_read_only");
            return false;
        }
        int32 CustomCount = 0;
        Op->TryGetNumberField(TEXT("custom_data_count"), CustomCount);
        TArray<Instances::FInstance> Desired;
        if (!Instances::Parse(Rows(Op, TEXT("instances")), CustomCount, Desired, Context.Error))
        {
            return false;
        }
        const FString Key = String(Op, TEXT("actor_id")) + TEXT("/") + Name;
        FObject Current;
        if (!Context.CreatedComponents.Contains(Key)
            && (!Instances::Export(Instanced, Current, Context.Error)
                || String(Current, TEXT("revision")) != String(Op, TEXT("expected_revision"))))
        {
            Context.Error = TEXT("instance_conflict_after_construction: ") + Name;
            return false;
        }
        return Instances::Apply(Instanced, Desired, CustomCount, Context.Error);
    }
    if (!WriteProperties(Component, Object(Op, TEXT("properties")), true, Context.Error))
    {
        return false;
    }
    Component = ResolveComponent(Actor, Name);
    if (!IsValid(Component))
    {
        Context.Error = TEXT("component_reconstructed: ") + Name;
        return false;
    }
    if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
    {
        if (Op->HasField(TEXT("parent")) && SceneComponent != Actor->GetRootComponent())
        {
            USceneComponent* Parent = Cast<USceneComponent>(ResolveComponent(Actor, String(Op, TEXT("parent"))));
            if (Parent)
            {
                if (!SceneComponent->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform))
                {
                    Context.Error = TEXT("component_attach_failed");
                    return false;
                }
            }
            else
            {
                if (!String(Op, TEXT("parent")).IsEmpty())
                {
                    Context.Error = TEXT("component_parent_missing_during_apply");
                    return false;
                }
                SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
            }
        }
        if (Op->HasField(TEXT("transform")) && SceneComponent != Actor->GetRootComponent())
        {
            FTransform Transform;
            if (!ReadTransform(String(Op, TEXT("transform")), Transform, Context.Error))
            {
                return false;
            }
            SceneComponent->SetRelativeTransform(Transform);
        }
    }
    Actor->MarkPackageDirty();
    return true;
}
}
