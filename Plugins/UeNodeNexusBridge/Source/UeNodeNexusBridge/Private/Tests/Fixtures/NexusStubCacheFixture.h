#pragma once

#include "Engine/DataAsset.h"
#include "NexusStubCacheFixture.generated.h"

UCLASS()
class UNexusStubCacheFixture : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category="Nexus Tests")
    int32 Marker = 0;
};
