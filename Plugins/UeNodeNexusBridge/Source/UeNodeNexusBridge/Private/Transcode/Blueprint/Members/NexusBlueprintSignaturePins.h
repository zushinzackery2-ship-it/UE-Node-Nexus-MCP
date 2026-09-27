#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

class UK2Node_EditablePinBase;

namespace UeNodeNexusBridge::Transcode
{
bool SetSignaturePins(UK2Node_EditablePinBase* Node, const TArray<TSharedPtr<FJsonValue>>* Params,
    EEdGraphPinDirection Direction, bool bInherited, FString& Error);
TArray<TSharedPtr<FJsonValue>> SignaturePinsJson(UK2Node_EditablePinBase* Node, EEdGraphPinDirection Direction);
}
