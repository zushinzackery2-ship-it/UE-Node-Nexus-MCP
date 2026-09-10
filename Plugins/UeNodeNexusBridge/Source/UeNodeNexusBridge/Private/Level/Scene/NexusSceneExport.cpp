#include "NexusSceneWorld.h"

#include "NexusSceneProperties.h"
#include "NexusSceneIdentity.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Level/Instances/NexusInstanceEdit.h"
#include "Level/Instances/NexusInstanceIdentity.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Scene
{
static void AddProperties(UObject* Target, const FObject& Json)
{
    FObject Properties, Defaults, Schema;
    ExportProperties(Target, Properties, Defaults, Schema);
    Json->SetObjectField(TEXT("properties"), Properties);
    Json->SetObjectField(TEXT("defaults"), Defaults);
    Json->SetObjectField(TEXT("property_schema"), Schema);
}

static FObject ExportComponent(UActorComponent* Component, bool bRebind, FString& Error)
{
    FObject Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), ComponentId(Component).ToString(EGuidFormats::Digits));
    Json->SetStringField(TEXT("name"), Component->GetName());
    Json->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
    Json->SetBoolField(TEXT("editable"), Component->CreationMethod != EComponentCreationMethod::UserConstructionScript);
    Json->SetBoolField(TEXT("removable"), Component->CreationMethod == EComponentCreationMethod::Instance
        && Component != Component->GetOwner()->GetRootComponent());
    AddProperties(Component, Json);
    if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
    {
        Json->SetStringField(TEXT("transform"), TransformText(SceneComponent->GetRelativeTransform()));
        USceneComponent* Parent = SceneComponent->GetAttachParent();
        Json->SetStringField(TEXT("parent"), Parent && Parent->GetOwner() == Component->GetOwner()
            ? ComponentId(Parent).ToString(EGuidFormats::Digits) : FString());
        Json->SetBoolField(TEXT("is_root"), Component == Component->GetOwner()->GetRootComponent());
    }
    if (UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component))
    {
        FObject InstanceData;
        if (!UeNodeNexusBridge::Instances::Export(Instances, InstanceData, Error, bRebind))
        {
            return nullptr;
        }
        Json->SetObjectField(TEXT("instance_data"), InstanceData);
    }
    return Json;
}

FObject ExportActor(AActor* Actor, bool bRebind, FString& Error)
{
    FObject Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("id"), Actor->GetActorGuid().ToString(EGuidFormats::Digits));
    Json->SetStringField(TEXT("scene_owner"), OwnerKey(Actor));
    Json->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Json->SetStringField(TEXT("level_path"), Actor->GetLevel()->GetOutermost()->GetName());
    UBlueprint* Blueprint = Cast<UBlueprint>(Actor->GetClass()->ClassGeneratedBy);
    Json->SetStringField(TEXT("class"), Blueprint ? Blueprint->GetPathName() : Actor->GetClass()->GetPathName());
    Json->SetStringField(TEXT("label"), Actor->GetActorLabel());
    Json->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
    AActor* Parent = Actor->GetAttachParentActor();
    Json->SetStringField(TEXT("parent"), Parent ? Parent->GetActorGuid().ToString(EGuidFormats::Digits) : FString());
    Json->SetStringField(TEXT("parent_path"), Parent ? Parent->GetPathName() : FString());
    Json->SetStringField(TEXT("transform"), TransformText(Parent && Actor->GetRootComponent()
        ? Actor->GetRootComponent()->GetRelativeTransform() : Actor->GetActorTransform()));
    AddProperties(Actor, Json);
    FRows Components;
    TArray<UActorComponent*> Sorted = Actor->GetComponents().Array();
    Sorted.Sort([](const UActorComponent& Left, const UActorComponent& Right)
    {
        return Left.GetName() < Right.GetName();
    });
    for (UActorComponent* Component : Sorted)
    {
        if (!IsValid(Component) || Component->IsEditorOnly() || Component->HasAnyFlags(RF_Transient))
        {
            continue;
        }
        FObject Item = ExportComponent(Component, bRebind, Error);
        if (!Item.IsValid())
        {
            return nullptr;
        }
        Components.Add(MakeShared<FJsonValueObject>(Item));
    }
    Json->SetArrayField(TEXT("components"), Components);
    return Json;
}

FObject ExportScene(UWorld* World, const FObject& Selector, FString& Error)
{
    FObject Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("kind"), TEXT("scene"));
    Json->SetStringField(TEXT("map_path"), World->GetOutermost()->GetName());
    Json->SetStringField(TEXT("name"), String(Selector, TEXT("name")));
    Json->SetStringField(TEXT("schema_key"), Transcode::SchemaKey());
    FRows Refs = Rows(Selector, TEXT("actors"));
    for (const auto& Path : Rows(Selector, TEXT("actor_paths")))
    {
        FString Text;
        if (!Path->TryGetString(Text))
        {
            Error = TEXT("invalid_actor_path");
            return nullptr;
        }
        FObject Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("id"), Text);
        Refs.Add(MakeShared<FJsonValueObject>(Ref));
    }
    bool bRebind = false;
    Selector->TryGetBoolField(TEXT("rebind"), bRebind);
    FRows Actors, Missing, Unavailable;
    TSet<FGuid> Seen;
    const FActorIndex Index(World);
    const FString Key = World->GetOutermost()->GetName() + TEXT("#") + String(Selector, TEXT("name"));
    bool bDirty = World->GetOutermost()->IsDirty();
    for (const auto& Value : Refs)
    {
        const FObject* Ref = nullptr;
        if (!Value->TryGetObject(Ref))
        {
            Error = TEXT("invalid_actor_reference");
            return nullptr;
        }
        AActor* Actor = Index.Find(String(*Ref, TEXT("id")));
        FGuid RequestedGuid;
        if ((FGuid::Parse(String(*Ref, TEXT("id")), RequestedGuid) && Index.AmbiguousGuids.Contains(RequestedGuid))
            || (Actor && Index.AmbiguousGuids.Contains(Actor->GetActorGuid())))
        {
            Error = TEXT("actor_identity_conflict: duplicate actor GUIDs in loaded levels");
            return nullptr;
        }
        if (!Actor)
        {
            (IsUnavailable(World, *Ref) ? Unavailable : Missing).Add(Value);
            continue;
        }
        if (!SupportedActor(Actor))
        {
            Error = TEXT("unsupported_scene_actor: ") + Actor->GetPathName();
            return nullptr;
        }
        const FString Owner = OwnerKey(Actor);
        if (!Owner.IsEmpty() && Owner != Key)
        {
            Error = TEXT("scene_membership_conflict: ") + Actor->GetPathName() + TEXT(" belongs to ") + Owner;
            return nullptr;
        }
        bDirty |= Actor->GetPackage()->IsDirty() || Actor->GetLevel()->GetOutermost()->IsDirty();
        if (Seen.Contains(Actor->GetActorGuid()))
        {
            continue;
        }
        Seen.Add(Actor->GetActorGuid());
        FObject Item = ExportActor(Actor, bRebind, Error);
        if (!Item.IsValid())
        {
            return nullptr;
        }
        Actors.Add(MakeShared<FJsonValueObject>(Item));
    }
    Actors.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
    {
        return String(Left->AsObject(), TEXT("id")) < String(Right->AsObject(), TEXT("id"));
    });
    Json->SetArrayField(TEXT("actors"), Actors);
    Json->SetStringField(TEXT("revision"), Digest(Json));
    Json->SetArrayField(TEXT("missing"), Missing);
    Json->SetArrayField(TEXT("unavailable"), Unavailable);
    Json->SetBoolField(TEXT("dirty"), bDirty);
    return Json;
}
}
