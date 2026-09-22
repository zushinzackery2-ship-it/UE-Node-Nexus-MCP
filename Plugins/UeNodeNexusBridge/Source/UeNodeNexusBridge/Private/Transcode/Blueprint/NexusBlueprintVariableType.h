#pragma once

#include "CoreMinimal.h"

class UBlueprint;
struct FEdGraphPinType;

namespace UeNodeNexusBridge::Transcode
{
// Change a member variable's type without the editor's confirmation dialog, and
// prove the change landed. See the .cpp for why the engine helper cannot be used
// from an unattended editor.
bool ChangeVariableType(UBlueprint* Blueprint, FName Name, const FEdGraphPinType& NewType, FString& OutError);
bool VerifyVariableType(UBlueprint* Blueprint, FName Name, const FEdGraphPinType& NewType, FString& OutError);
}
