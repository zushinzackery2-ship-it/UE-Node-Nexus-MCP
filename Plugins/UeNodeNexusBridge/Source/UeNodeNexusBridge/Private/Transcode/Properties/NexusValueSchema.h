#pragma once

#include "CoreMinimal.h"

class FProperty;
class FJsonObject;
class UScriptStruct;

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> ValueSchema(FProperty* Property);
TSharedPtr<FJsonObject> StructValueSchema(UScriptStruct* Struct);
}
