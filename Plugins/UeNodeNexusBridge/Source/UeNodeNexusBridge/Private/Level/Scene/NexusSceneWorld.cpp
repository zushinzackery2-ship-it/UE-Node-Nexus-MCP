#include "NexusSceneWorld.h"
#include "NexusSceneIdentity.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "WorldPartition/WorldPartition.h"

namespace UeNodeNexusBridge::Scene
{
FActorIndex::FActorIndex(UWorld* World)
{
    for (TActorIterator<AActor> It(World, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
    {
        Paths.Add(It->GetPathName(), *It);
        if (!It->GetLevel()->IsInstancedLevel())
        {
            if (Guids.Contains(It->GetActorGuid()))
            {
                AmbiguousGuids.Add(It->GetActorGuid());
            }
            Guids.Add(It->GetActorGuid(), *It);
        }
    }
}

AActor* FActorIndex::Find(const FString& Id) const
{
    FGuid Guid;
    return FGuid::Parse(Id, Guid) ? (AmbiguousGuids.Contains(Guid) ? nullptr : Guids.FindRef(Guid)) : Paths.FindRef(Id);
}

UWorld* ResolveWorld(const FString& Map, FString& Error)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->WorldType != EWorldType::Editor || GEditor->PlayWorld
        || World->GetOutermost()->GetName() != FPackageName::ObjectPathToPackageName(Map))
    {
        Error = TEXT("world_unavailable: target map must be open in the editor outside PIE");
        return nullptr;
    }
    return World;
}

ULevel* ResolveLevel(UWorld* World, const FString& Package)
{
    for (ULevel* Level : World->GetLevels())
    {
        if (Level && !Level->IsInstancedLevel() && Level->GetOutermost()->GetName() == FPackageName::ObjectPathToPackageName(Package))
        {
            return Level;
        }
    }
    return nullptr;
}

UActorComponent* ResolveComponent(AActor* Actor, const FString& Name)
{
    if (!Actor)
    {
        return nullptr;
    }
    for (UActorComponent* Component : Actor->GetComponents())
    {
        if (Component && (Component->GetName() == Name || ComponentId(Component).ToString(EGuidFormats::Digits) == Name))
        {
            return Component;
        }
    }
    return nullptr;
}

UClass* ResolveActorClass(const FString& Path)
{
    if (Path.StartsWith(TEXT("/Game/")))
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Path));
        return Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()) ? Blueprint->GeneratedClass : nullptr;
    }
    const FString Qualified = Path.Contains(TEXT("/")) ? Path : TEXT("/Script/Engine.") + Path;
    return LoadClass<AActor>(nullptr, *Qualified);
}

bool IsUnavailable(UWorld* World, const FObject& Ref)
{
    if (!ResolveLevel(World, String(Ref, TEXT("level_path"))))
    {
        return true;
    }
    FGuid Guid;
    UWorldPartition* Partition = World->GetWorldPartition();
    return Partition && FGuid::Parse(String(Ref, TEXT("id")), Guid) && Partition->GetActorDescInstance(Guid) != nullptr;
}

UActorComponent* ComponentTemplate(UClass* ActorClass, const FString& Name)
{
    if (UActorComponent* Native = ResolveComponent(Cast<AActor>(ActorClass->GetDefaultObject()), Name))
    {
        return Native;
    }
    for (UClass* Type = ActorClass; Type; Type = Type->GetSuperClass())
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Type->ClassGeneratedBy);
        if (!Blueprint || !Blueprint->SimpleConstructionScript)
        {
            continue;
        }
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node->GetVariableName().ToString() == Name)
            {
                return Node->ComponentTemplate;
            }
        }
    }
    return nullptr;
}

bool SupportedClass(UClass* Class)
{
    if (!Class || !Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
    {
        return false;
    }
    for (UClass* Type = Class; Type; Type = Type->GetSuperClass())
    {
        const FString Name = Type->GetName();
        if (Name == TEXT("InstancedFoliageActor") || Name == TEXT("LevelInstance")
            || Name == TEXT("PackedLevelActor") || Name == TEXT("WorldSettings") || Name == TEXT("LevelScriptActor"))
        {
            return false;
        }
    }
    return true;
}

bool SupportedActor(AActor* Actor)
{
    if (!IsValid(Actor) || Actor->IsTemplate() || Actor->HasAnyFlags(RF_Transient) || !SupportedClass(Actor->GetClass())
        || !Actor->GetActorGuid().IsValid() || Actor->GetLevel()->IsInstancedLevel())
    {
        return false;
    }
    for (UActorComponent* Component : Actor->GetComponents())
    {
        for (UClass* Type = Component->GetClass(); Type; Type = Type->GetSuperClass())
        {
            if (Type->GetName() == TEXT("PCGComponent"))
            {
                return false;
            }
        }
    }
    return true;
}
}
