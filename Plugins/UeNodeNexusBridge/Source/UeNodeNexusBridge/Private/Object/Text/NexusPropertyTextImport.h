#pragma once

#include "CoreMinimal.h"

class FProperty;

namespace UeNodeNexusBridge
{
bool ImportCompletePropertyText(const FProperty* Property, void* Storage, UObject* Owner,
    const FString& Text, FString& Error);
}
