#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::RuntimeSmoke
{
TSharedPtr<FJsonObject> Evaluate(const TSharedPtr<FJsonObject>& Runtime, const FString& ExpectedSession);
}
