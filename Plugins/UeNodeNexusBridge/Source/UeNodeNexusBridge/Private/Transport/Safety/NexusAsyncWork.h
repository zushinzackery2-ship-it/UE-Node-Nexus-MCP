#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::Safety
{
FString EngineBusyReason();
FString AsyncBusyReason();
bool BeginAsync(const FString& RequestId, const FString& Operation);
bool Holds(const FString& RequestId);
void FinishAsync(const FString& RequestId, bool bOk, const FString& Code);
TSharedPtr<FJsonObject> AsyncSnapshot();
}
