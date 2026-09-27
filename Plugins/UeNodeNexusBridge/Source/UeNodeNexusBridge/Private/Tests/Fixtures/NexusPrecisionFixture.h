#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NexusPrecisionFixture.generated.h"

USTRUCT()
struct FNexusPrecisionValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere)
    float Tiny = 1.0e-12f;

    UPROPERTY(EditAnywhere)
    double Precise = 1.2345678901234567;

    UPROPERTY(EditAnywhere)
    FString Literal = TEXT("0.000001, (Value=1.200000)");

    UPROPERTY(EditAnywhere)
    FTransform Transform;
};

UCLASS(Transient)
class UNexusPrecisionFixture : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere)
    float Scalar = 1.0e-12f;

    UPROPERTY(EditAnywhere)
    double Double = 1.0e-100;

    UPROPERTY(EditAnywhere)
    FNexusPrecisionValue Nested;

    UPROPERTY(EditAnywhere)
    TArray<FNexusPrecisionValue> Array;

    UPROPERTY(EditAnywhere)
    TSet<double> Set;

    UPROPERTY(EditAnywhere)
    TMap<FString, FNexusPrecisionValue> Map;
};
