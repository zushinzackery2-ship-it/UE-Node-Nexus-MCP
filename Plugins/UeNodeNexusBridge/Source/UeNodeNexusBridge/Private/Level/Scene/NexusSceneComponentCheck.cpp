#include "NexusSceneApply.h"

#include "NexusSceneProperties.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Level/Instances/NexusInstanceEdit.h"
#include "Level/Instances/NexusInstanceIdentity.h"

namespace UeNodeNexusBridge::Scene
{
static bool AddComponentSpec(FApply& Context, const FObject& Op, const FString& Owner, const FString& Key)
{
    const FString Name = String(Op, TEXT("name"));
    UClass* Type = LoadClass<UActorComponent>(nullptr, *String(Op, TEXT("class")));
    FText Reason;
    if (Name.IsEmpty() || !FName(*Name).IsValidObjectName(Reason) || !Type
        || Type->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) || Type->GetDefaultObject()->IsEditorOnly()
        || Context.ComponentTypes.Contains(Key))
    {
        Context.Error = TEXT("invalid_component_creation: ") + Name;
        return false;
    }
    for (const auto& Pair : Context.ComponentNames)
    {
        if (Pair.Key.StartsWith(Owner + TEXT("/")) && Pair.Value == Name)
        {
            Context.Error = TEXT("component_name_conflict: ") + Name;
            return false;
        }
    }
    UActorComponent* Template = nullptr;
    if (Context.CreatedActors.Contains(Owner))
    {
        Template = ComponentTemplate(Context.Classes[Owner], Name);
        if (Template && Template->GetClass() != Type)
        {
            Context.Error = TEXT("component_template_class_conflict: ") + Name;
            return false;
        }
    }
    Context.ComponentTypes.Add(Key, Type);
    Context.ComponentTemplates.Add(Key, Template ? Template : Cast<UActorComponent>(Type->GetDefaultObject()));
    Context.ComponentNames.Add(Key, Name);
    Context.CreatedComponents.Add(Key);
    return true;
}

static bool CheckInstances(FApply& Context, const FObject& Op, UActorComponent* Template, const FString& Key)
{
    int32 Count = 0;
    TArray<Instances::FInstance> Desired;
    const FRows* RowsValue = nullptr;
    if (!Cast<UInstancedStaticMeshComponent>(Template) || Instances::IsConstructionOwned(Template)
        || !Op->TryGetNumberField(TEXT("custom_data_count"), Count) || !Op->TryGetArrayField(TEXT("instances"), RowsValue)
        || !Instances::Parse(*RowsValue, Count, Desired, Context.Error))
    {
        if (Context.Error.IsEmpty())
        {
            Context.Error = TEXT("invalid_or_construction_owned_instances: ") + Key;
        }
        return false;
    }
    const FObject Before = Object(Context.ComponentSnapshots.FindRef(Key), TEXT("instance_data"));
    if (!Context.CreatedComponents.Contains(Key) && String(Op, TEXT("expected_revision")) != String(Before, TEXT("revision")))
    {
        Context.Error = TEXT("instance_conflict: ") + Key;
        return false;
    }
    bool bAllowDelete = false;
    Context.Plan->TryGetBoolField(TEXT("allow_delete"), bAllowDelete);
    if (!bAllowDelete)
    {
        TSet<FGuid> Wanted;
        for (const auto& Item : Desired)
        {
            Wanted.Add(Item.Id);
        }
        for (const auto& Value : Rows(Before, TEXT("instances")))
        {
            FGuid Id;
            FGuid::Parse(String(Value->AsObject(), TEXT("id")), Id);
            if (!Wanted.Contains(Id))
            {
                Context.Error = TEXT("delete_not_allowed: instance removal");
                return false;
            }
        }
    }
    return true;
}

bool CheckComponent(FApply& Context, const FObject& Op)
{
    const FString Owner = String(Op, TEXT("actor_id"));
    const FString Key = Owner + TEXT("/") + String(Op, TEXT("id"));
    const FString Verb = String(Op, TEXT("op"));
    if (!Context.Classes.Contains(Owner) || Context.RemovedComponents.Contains(Key))
    {
        Context.Error = TEXT("unknown_component_owner_or_removed_component: ") + Key;
        return false;
    }
    if (Verb == TEXT("create_component") && !AddComponentSpec(Context, Op, Owner, Key))
    {
        return false;
    }
    UActorComponent* Template = Context.ComponentTemplates.FindRef(Key);
    if (!Template || Template->CreationMethod == EComponentCreationMethod::UserConstructionScript)
    {
        Context.Error = TEXT("component_unavailable_or_construction_owned: ") + Key;
        return false;
    }
    AActor* OwnerActor = Context.Actors.FindRef(Owner);
    if ((Op->HasField(TEXT("parent")) || Op->HasField(TEXT("transform")))
        && (!Template->IsA<USceneComponent>() || (OwnerActor && Template == OwnerActor->GetRootComponent())))
    {
        Context.Error = TEXT("invalid_component_transform: root transforms belong to the actor");
        return false;
    }
    if (Verb == TEXT("remove_component"))
    {
        AActor* Actor = Context.Actors.FindRef(Owner);
        if (!Actor || Template->CreationMethod != EComponentCreationMethod::Instance || Template == Actor->GetRootComponent())
        {
            Context.Error = TEXT("inherited_or_root_component_not_removable: ") + Key;
            return false;
        }
        Context.RemovedComponents.Add(Key);
    }
    if (Verb == TEXT("instances_set"))
    {
        return CheckInstances(Context, Op, Template, Key);
    }
    return WriteProperties(Template, Object(Op, TEXT("properties")), false, Context.Error);
}
}
