#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "NexusRuntimeDiagnosticsLibrary.generated.h"

UCLASS()
class UENODENEXUSBRIDGE_API UNexusRuntimeDiagnosticsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString RuntimeDiagnosticsJson(const FString& SessionId = TEXT(""));

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString RuntimeVerificationJson(const FString& SessionId, const FString& InstanceId, bool bFunctionalPassed);
};
