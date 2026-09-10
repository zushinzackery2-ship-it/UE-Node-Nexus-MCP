#pragma once

#include "Engine/AssetUserData.h"
#include "NexusInstanceData.generated.h"

UCLASS()
class UNexusInstanceData : public UAssetUserData
{
    GENERATED_BODY()

public:
    UPROPERTY()
    FGuid ActorGuid;

    UPROPERTY()
    FGuid ComponentGuid;

    UPROPERTY()
    TArray<FGuid> Ids;

    UPROPERTY()
    bool bIdentityValid = true;

    virtual bool IsEditorOnly() const override
    {
        return true;
    }
};
