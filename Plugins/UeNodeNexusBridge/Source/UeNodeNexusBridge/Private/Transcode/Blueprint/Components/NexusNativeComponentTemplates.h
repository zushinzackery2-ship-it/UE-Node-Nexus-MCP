#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class UBlueprint;

namespace UeNodeNexusBridge::Transcode
{
UActorComponent* FindNativeComponentTemplate(UBlueprint* Blueprint, const FName Name);
TArray<FString> NativeComponentAliases(const UActorComponent* Component);
}
