#pragma once

#include "NexusSceneJson.h"

class UWorld;
class ULevel;
class AActor;
class UActorComponent;

namespace UeNodeNexusBridge::Scene
{
struct FActorIndex
{
    TMap<FGuid, AActor*> Guids;
    TMap<FString, AActor*> Paths;
    TSet<FGuid> AmbiguousGuids;

    explicit FActorIndex(UWorld* World);
    AActor* Find(const FString& Id) const;
};

UWorld* ResolveWorld(const FString& Map, FString& Error);
ULevel* ResolveLevel(UWorld* World, const FString& Package);
UActorComponent* ResolveComponent(AActor* Actor, const FString& Name);
UActorComponent* ComponentTemplate(UClass* ActorClass, const FString& Name);
UClass* ResolveActorClass(const FString& Path);
bool SupportedClass(UClass* Type);
bool IsUnavailable(UWorld* World, const FObject& Ref);
bool SupportedActor(AActor* Actor);
FObject ExportScene(UWorld* World, const FObject& Selector, FString& Error);
FObject ExportActor(AActor* Actor, bool bRebind, FString& Error);
}
