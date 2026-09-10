#include "NexusSceneApply.h"

#include "NexusSceneProperties.h"
#include "NexusSceneIdentity.h"
#include "NexusSceneData.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace UeNodeNexusBridge::Scene
{
static bool SetActor(FApply& Context, AActor* Actor, const FObject& Op)
{
    Context.Touch(Actor);
    Actor->Modify();
    const FObject Properties = Object(Op, TEXT("properties"));
    if (!WriteProperties(Actor, Properties, true, Context.Error, false))
    {
        return false;
    }
    if (Op->HasField(TEXT("folder")))
    {
        Actor->SetFolderPath(FName(*String(Op, TEXT("folder"))));
    }
    if (Op->HasField(TEXT("parent")))
    {
        const FString ParentId = String(Op, TEXT("parent"));
        AActor* Parent = Context.Actors.FindRef(ParentId);
        if (!Parent && !ParentId.IsEmpty())
        {
            Parent = Context.WorldIndex->Find(ParentId);
        }
        if (IsValid(Parent))
        {
            if (!Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepRelativeTransform))
            {
                Context.Error = TEXT("actor_attach_failed");
                return false;
            }
        }
        else
        {
            if (!ParentId.IsEmpty())
            {
                Context.Error = TEXT("parent_actor_missing_during_apply");
                return false;
            }
            Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        }
    }
    if (Op->HasField(TEXT("transform")))
    {
        FTransform Transform;
        if (!ReadTransform(String(Op, TEXT("transform")), Transform, Context.Error))
        {
            return false;
        }
        if (Actor->GetAttachParentActor() && Actor->GetRootComponent())
        {
            Actor->GetRootComponent()->SetRelativeTransform(Transform);
        }
        else
        {
            if (Actor->GetRootComponent())
            {
                Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
            }
            else if (!Transform.Equals(FTransform::Identity))
            {
                Context.Error = TEXT("actor_root_required_for_transform");
                return false;
            }
        }
    }
    const bool bLabelChanged = Op->HasField(TEXT("label")) && Actor->GetActorLabel() != String(Op, TEXT("label"));
    if (bLabelChanged)
    {
        // SetActorLabel issues the single PostEditChange used to rerun construction.
        Actor->SetActorLabel(String(Op, TEXT("label")));
    }
    else if (!Properties->Values.IsEmpty() || Op->HasField(TEXT("transform")) || Op->HasField(TEXT("parent")))
    {
        Actor->PostEditChange();
    }
    Actor->MarkPackageDirty();
    Context.bChanged = true;
    return true;
}

bool ApplyActor(FApply& Context, const FObject& Op)
{
    const FString Verb = String(Op, TEXT("op"));
    const FString Id = String(Op, TEXT("id"));
    AActor* Actor = Context.Actors.FindRef(Id);
    if (Verb == TEXT("create_actor"))
    {
        FActorSpawnParameters Params;
        Params.OverrideLevel = ResolveLevel(Context.World, String(Op, TEXT("level_path")));
        Params.ObjectFlags = RF_Transactional;
        FGuid::Parse(Id, Params.OverrideActorGuid);
        Params.bCreateActorPackage = Params.OverrideLevel->IsUsingExternalActors();
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.OverrideLevel->Modify();
        Actor = Context.World->SpawnActor<AActor>(ResolveActorClass(String(Op, TEXT("class"))), FTransform::Identity, Params);
        if (!Actor)
        {
            Context.Error = TEXT("actor_spawn_failed");
            return false;
        }
        Context.Actors.Add(Id, Actor);
        Context.Touch(Actor);
        Context.bChanged = true;
        UNexusSceneData* Data = EnsureMetadata(Actor);
        Data->Modify();
        Data->SceneKey = String(Context.Plan, TEXT("map_path")) + TEXT("#") + String(Context.Plan, TEXT("name"));
        return true;
    }
    if (!IsValid(Actor))
    {
        Context.Error = TEXT("actor_missing_during_apply: ") + Id;
        return false;
    }
    if (Verb == TEXT("delete_actor"))
    {
        Context.Touch(Actor, true);
        UEditorActorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
        if (!Subsystem || !Subsystem->DestroyActor(Actor))
        {
            Context.Error = TEXT("actor_delete_failed");
            return false;
        }
        Context.Actors.Remove(Id);
        Context.bChanged = true;
        return true;
    }
    return SetActor(Context, Actor, Op);
}
}
