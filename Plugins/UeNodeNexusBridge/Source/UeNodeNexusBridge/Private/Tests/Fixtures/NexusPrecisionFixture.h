#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NexusPrecisionFixture.generated.h"

USTRUCT()
struct FNexusPrecisionValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    float Tiny = 1.0e-12f;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    double Precise = 1.2345678901234567;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    FString Literal = TEXT("0.000001, (Value=1.200000)");

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    FTransform Transform;
};

UCLASS(Transient)
class UNexusPrecisionFixture : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    float Scalar = 1.0e-12f;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    double Double = 1.0e-100;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    FNexusPrecisionValue Nested;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    TArray<FNexusPrecisionValue> Array;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    TSet<double> Set;

    UPROPERTY(EditAnywhere, Category="Nexus Tests")
    TMap<FString, FNexusPrecisionValue> Map;
};
