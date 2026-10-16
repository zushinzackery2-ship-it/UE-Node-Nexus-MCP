#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "NexusSkeletonAuthoringLibrary.generated.h"

class USkeleton;

UCLASS()
class UENODENEXUSBRIDGE_API UNexusSkeletonAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Editor authoring only. JSON must explicitly provide every reference bone.
    UFUNCTION(BlueprintCallable, Category="Nexus|Animation|Authoring")
    static FString WriteBlendMaskJson(USkeleton* Skeleton, FName ProfileName,
        const FString& WeightsJson, bool ReplaceExisting = false, bool DryRun = true);

    UFUNCTION(BlueprintCallable, Category="Nexus|Animation|Authoring")
    static FString ReadBlendMaskJson(USkeleton* Skeleton, FName ProfileName);
};
