#include "NexusSceneApply.h"

#include "GameFramework/Actor.h"

namespace UeNodeNexusBridge::Scene
{
static bool CheckCycles(const TMap<FString, FString>& Parents, FString& Error)
{
    TSet<FString> Done;
    for (const auto& Pair : Parents)
    {
        FString Current = Pair.Key;
        TSet<FString> Chain;
        while (Parents.Contains(Current) && !Done.Contains(Current))
        {
            if (Chain.Contains(Current))
            {
                Error = TEXT("attachment_cycle: ") + Current;
                return false;
            }
            Chain.Add(Current);
            Current = Parents[Current];
        }
        Done.Append(Chain);
    }
    return true;
}

bool CheckHierarchy(FApply& Context)
{
    const FActorIndex& Index = *Context.WorldIndex;
    TMap<FString, FString> Parents;
    TMap<FString, FString> Components;
    TSet<FString> Deleted;
    for (const auto& Pair : Index.Guids)
    {
        AActor* Parent = Pair.Value->GetAttachParentActor();
        Parents.Add(Pair.Key.ToString(EGuidFormats::Digits), Parent ? Parent->GetActorGuid().ToString(EGuidFormats::Digits) : FString());
    }
    for (const FString& Id : Context.CreatedActors)
    {
        Parents.Add(Id, FString());
    }
    for (const auto& Actor : Rows(Context.Before, TEXT("actors")))
    {
        const FString Owner = String(Actor->AsObject(), TEXT("id")) + TEXT("/");
        for (const auto& Component : Rows(Actor->AsObject(), TEXT("components")))
        {
            const FString Parent = String(Component->AsObject(), TEXT("parent"));
            Components.Add(Owner + String(Component->AsObject(), TEXT("id")), Parent.IsEmpty() ? FString() : Owner + Parent);
        }
    }
    for (const FString& Key : Context.CreatedComponents)
    {
        Components.Add(Key, FString());
    }
    for (const auto& Value : Rows(Context.Plan, TEXT("ops")))
    {
        const FObject Op = Value->AsObject();
        const FString Verb = String(Op, TEXT("op"));
        const FString Id = String(Op, TEXT("id"));
        if (Verb == TEXT("delete_actor"))
        {
            Deleted.Add(Id);
        }
        else if (Verb == TEXT("update_actor") && Op->HasField(TEXT("parent")))
        {
            FString Parent = String(Op, TEXT("parent"));
            if (!Parent.IsEmpty() && !Parents.Contains(Parent))
            {
                AActor* External = Index.Find(Parent);
                if (!SupportedActor(External))
                {
                    Context.Error = TEXT("parent_actor_unavailable: ") + Parent;
                    return false;
                }
                Parent = External->GetActorGuid().ToString(EGuidFormats::Digits);
            }
            Parents[Id] = Parent;
        }
        else if ((Verb == TEXT("update_component") || Verb == TEXT("create_component")) && Op->HasField(TEXT("parent")))
        {
            const FString Owner = String(Op, TEXT("actor_id")) + TEXT("/");
            const FString Parent = String(Op, TEXT("parent"));
            if (!Parent.IsEmpty() && !Components.Contains(Owner + Parent))
            {
                Context.Error = TEXT("component_parent_unavailable: ") + Parent;
                return false;
            }
            Components[Owner + Id] = Parent.IsEmpty() ? FString() : Owner + Parent;
        }
    }
    for (const auto& Pair : Parents)
    {
        if (Deleted.Contains(Pair.Value) && !Deleted.Contains(Pair.Key))
        {
            Context.Error = TEXT("deletion_would_change_attached_actor: ") + Pair.Key;
            return false;
        }
    }
    for (const auto& Pair : Components)
    {
        if (Context.RemovedComponents.Contains(Pair.Value) && !Context.RemovedComponents.Contains(Pair.Key))
        {
            Context.Error = TEXT("deletion_would_change_attached_component: ") + Pair.Key;
            return false;
        }
    }
    TSet<FString> Expected;
    for (const auto& Pair : Context.Classes)
    {
        if (!Deleted.Contains(Pair.Key))
        {
            Expected.Add(Pair.Key);
        }
    }
    TSet<FString> Declared;
    for (const auto& Value : Rows(Context.Plan, TEXT("actors")))
    {
        const FObject* Row = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Row) || !Expected.Contains(String(*Row, TEXT("id"))))
        {
            Context.Error = TEXT("invalid_final_scene_membership");
            return false;
        }
        Declared.Add(String(*Row, TEXT("id")));
    }
    if (Declared.Num() != Expected.Num() || Declared.Num() != Rows(Context.Plan, TEXT("actors")).Num())
    {
        Context.Error = TEXT("incomplete_final_scene_membership");
        return false;
    }
    return CheckCycles(Parents, Context.Error) && CheckCycles(Components, Context.Error);
}
}
