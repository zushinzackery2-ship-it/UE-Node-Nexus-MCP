// UEditorActorSubsystem stub for compiler-only checks. Signatures mirror the
// documented UE 5.5 editor actor subsystem; see CoreTypes.h.
#pragma once

#include "GameFramework/Actor.h"
#include "Math/UeMathTypes.h"
#include "UObject/Object.h"

class UEditorActorSubsystem : public UObject
{
public:
    AActor* SpawnActorFromClass(
        TSubclassOf<AActor> ActorClass,
        FVector Location,
        FRotator Rotation = FRotator(),
        bool bTransient = false);

    bool DestroyActor(AActor* ActorToDestroy);
};
