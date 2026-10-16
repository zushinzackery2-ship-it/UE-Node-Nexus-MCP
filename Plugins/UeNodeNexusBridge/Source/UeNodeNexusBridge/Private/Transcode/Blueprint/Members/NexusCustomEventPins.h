#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UK2Node_CustomEvent;

namespace UeNodeNexusBridge::Transcode
{
bool ConfigureCustomEventPins(UK2Node_CustomEvent* Event, const TSharedPtr<FJsonObject>& Operation,
    FString& Error);
}
