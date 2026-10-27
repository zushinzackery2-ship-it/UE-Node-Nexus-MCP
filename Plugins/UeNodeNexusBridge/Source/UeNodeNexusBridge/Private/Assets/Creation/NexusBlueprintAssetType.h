#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UClass;
class UObject;
class UPackage;

namespace UeNodeNexusBridge
{
// Resolve what kind of Blueprint a parent class (and an optional explicit
// ``blueprint_type``) asks for, and whether it can be created at all. Callers
// use Check in dry run and Create for real, so both answer the same question.
bool CheckBlueprintAsset(const FString& ParentClassPath, const FString& RequestedType, FString& OutError);
UObject* CreateTypedBlueprintAsset(UPackage* Package, FName AssetName, const FString& ParentClassPath, const FString& RequestedType, FString& OutError);

// The ``blueprint_type`` name of an existing Blueprint, for the text mirror.
FString BlueprintTypeName(const UBlueprint* Blueprint);
// Creatable ``blueprint_type`` values, for error details and schema text.
TArray<FString> BlueprintTypeNames();
}
