#pragma once

#include "CoreMinimal.h"
#include "NexusNiagaraLwcFixture.generated.h"

USTRUCT()
struct FNexusNiagaraLwcFixture
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    double Scalar = 1.25;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    int32 Flag = 11;
};
