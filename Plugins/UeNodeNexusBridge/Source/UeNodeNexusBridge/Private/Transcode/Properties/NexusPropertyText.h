#pragma once

#include "CoreMinimal.h"

class FProperty;

namespace UeNodeNexusBridge::Transcode
{
FString ExportPrecisePropertyText(const FProperty* Property, const void* Value, UObject* Owner, int32 Flags = PPF_None);
}
