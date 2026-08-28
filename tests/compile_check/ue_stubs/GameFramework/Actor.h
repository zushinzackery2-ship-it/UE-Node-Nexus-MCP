// AActor stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Math/UeMathTypes.h"
#include "UObject/Object.h"

class AActor : public UObject
{
public:
    FString GetActorLabel() const;
    void SetActorLabel(const FString& NewActorLabel, bool bMarkDirty = true);
    bool SetActorLocation(const FVector& NewLocation);
    bool SetActorRotation(FRotator NewRotation);
    void SetActorScale3D(FVector NewScale3D);
    const FTransform& GetActorTransform() const;

    static UClass* StaticClass();
};
