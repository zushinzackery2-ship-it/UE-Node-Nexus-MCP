#include "NexusSceneApply.h"

#include "NexusSceneIdentity.h"
#include "NexusSceneProperties.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

namespace UeNodeNexusBridge::Scene
{
static bool LoadMembers(FApply& Context, const FActorIndex& Index)
{
    for (const auto& Value : Rows(Context.Before, TEXT("actors")))
    {
        const FObject Row = Value->AsObject();
        const FString Id = String(Row, TEXT("id"));
        AActor* Actor = Index.Find(Id);
        if (!SupportedActor(Actor))
        {
            Context.Error = TEXT("actor_unavailable: ") + Id;
            return false;
        }
        Context.Actors.Add(Id, Actor);
        Context.Classes.Add(Id, Actor->GetClass());
        for (const auto& Entry : Rows(Row, TEXT("components")))
        {
            const FObject ComponentRow = Entry->AsObject();
            const FString Key = Id + TEXT("/") + String(ComponentRow, TEXT("id"));
            UActorComponent* Component = ResolveComponent(Actor, String(ComponentRow, TEXT("id")));
            if (!Component)
            {
                Context.Error = TEXT("component_unavailable: ") + Key;
                return false;
            }
            Context.ComponentTypes.Add(Key, Component->GetClass());
            Context.ComponentTemplates.Add(Key, Component);
            Context.ComponentNames.Add(Key, Component->GetName());
            Context.ComponentSnapshots.Add(Key, ComponentRow);
        }
    }
    return true;
}

static bool CheckActor(FApply& Context, const FObject& Op, const FActorIndex& Index)
{
    const FString Id = String(Op, TEXT("id"));
    const FString Verb = String(Op, TEXT("op"));
    if (Verb == TEXT("create_actor"))
    {
        UClass* Type = ResolveActorClass(String(Op, TEXT("class")));
        if (!SupportedClass(Type) || Context.Classes.Contains(Id) || Index.Find(Id)
            || !ResolveLevel(Context.World, String(Op, TEXT("level_path"))))
        {
            Context.Error = TEXT("invalid_actor_creation: ") + Id;
            return false;
        }
        UBlueprint* Blueprint = Cast<UBlueprint>(Type->ClassGeneratedBy);
        if (Blueprint && (Blueprint->Status == BS_Error || Blueprint->Status == BS_Dirty || Blueprint->bBeingCompiled))
        {
            Context.Error = TEXT("blueprint_not_ready: ") + Blueprint->GetPathName();
            return false;
        }
        Context.Classes.Add(Id, Type);
        Context.CreatedActors.Add(Id);
    }
    UClass* Type = Context.Classes.FindRef(Id);
    AActor* Actor = Context.Actors.FindRef(Id);
    if (!Type || (Verb == TEXT("delete_actor") && (!Actor || Context.CreatedActors.Contains(Id))))
    {
        Context.Error = TEXT("actor_not_in_scene: ") + Id;
        return false;
    }
    static const TCHAR* Fields[] =
    {
        TEXT("label"), TEXT("folder"), TEXT("parent")
    };
    for (const TCHAR* Field : Fields)
    {
        FString Text;
        if (Op->HasField(Field) && !Op->TryGetStringField(Field, Text))
        {
            Context.Error = TEXT("invalid_actor_field: ") + FString(Field);
            return false;
        }
    }
    return WriteProperties(Actor ? static_cast<UObject*>(Actor) : Type->GetDefaultObject(),
        Object(Op, TEXT("properties")), false, Context.Error);
}

bool Preflight(FApply& Context)
{
    Context.WorldIndex = MakeUnique<FActorIndex>(Context.World);
    const FActorIndex& Index = *Context.WorldIndex;
    if (!LoadMembers(Context, Index))
    {
        return false;
    }
    bool bAllowDelete = false;
    Context.Plan->TryGetBoolField(TEXT("allow_delete"), bAllowDelete);
    const FRows* Operations = nullptr;
    if (!Context.Plan->TryGetArrayField(TEXT("ops"), Operations))
    {
        Context.Error = TEXT("invalid_scene_operations");
        return false;
    }
    for (const auto& Value : *Operations)
    {
        const FObject* Op = nullptr;
        FGuid Guid;
        if (!Value.IsValid() || !Value->TryGetObject(Op) || !FGuid::Parse(String(*Op, TEXT("id")), Guid) || !Guid.IsValid())
        {
            Context.Error = TEXT("invalid_scene_operation_identity");
            return false;
        }
        const FString Verb = String(*Op, TEXT("op"));
        FString TransformTextValue;
        FTransform Transform;
        if ((*Op)->HasField(TEXT("transform")) && (!(*Op)->TryGetStringField(TEXT("transform"), TransformTextValue)
            || TransformTextValue.IsEmpty() || !ReadTransform(TransformTextValue, Transform, Context.Error)))
        {
            Context.Error = TEXT("invalid_scene_transform");
            return false;
        }
        if ((Verb == TEXT("delete_actor") || Verb == TEXT("remove_component")) && !bAllowDelete)
        {
            Context.Error = TEXT("delete_not_allowed");
            return false;
        }
        if (Verb == TEXT("create_actor") || Verb == TEXT("update_actor") || Verb == TEXT("delete_actor"))
        {
            if (!CheckActor(Context, *Op, Index))
            {
                return false;
            }
        }
        else if (Verb == TEXT("create_component") || Verb == TEXT("update_component") || Verb == TEXT("remove_component") || Verb == TEXT("instances_set"))
        {
            if (!CheckComponent(Context, *Op))
            {
                return false;
            }
        }
        else
        {
            Context.Error = TEXT("unknown_scene_operation: ") + Verb;
            return false;
        }
    }
    return CheckHierarchy(Context);
}
}
