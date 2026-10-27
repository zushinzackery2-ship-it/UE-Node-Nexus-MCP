#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FProperty;
class UBlueprint;
class USCS_Node;

namespace UeNodeNexusBridge
{
bool ReadBlueprintNodeStringField(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& FieldName,
    FString& OutValue);
bool ReadBlueprintNodeBoolField(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& FieldName,
    bool& OutValue);
bool ReadBlueprintVariableName(
    const TSharedPtr<FJsonObject>& Payload,
    FString& OutValue);
USCS_Node* FindBlueprintComponentNode(UBlueprint* Blueprint, FName VariableName);
FProperty* FindBlueprintProperty(UBlueprint* Blueprint, FName VariableName);
bool BlueprintVariableExists(UBlueprint* Blueprint, const FString& VariableName);
}
