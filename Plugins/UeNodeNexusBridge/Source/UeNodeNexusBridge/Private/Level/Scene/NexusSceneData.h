#pragma once

#include "Components/ActorComponent.h"
#include "Engine/AssetUserData.h"
#include "NexusSceneData.generated.h"

UCLASS(NotBlueprintable)
class UNexusSceneData : public UActorComponent
{
    GENERATED_BODY()

public:
    UNexusSceneData()
    {
        bIsEditorOnly = true;
        bAutoActivate = false;
        PrimaryComponentTick.bCanEverTick = false;
    }

    UPROPERTY()
    FGuid ActorGuid;

    UPROPERTY()
    FString SceneKey;

    UPROPERTY()
    TMap<FName, FGuid> ComponentIds;

    UPROPERTY()
    TMap<FName, FString> ComponentClasses;
};

UCLASS()
class UNexusComponentData : public UAssetUserData
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FGuid ActorGuid;

    UPROPERTY()
    FGuid Id;

    virtual bool IsEditorOnly() const override
    {
        return true;
    }
};
