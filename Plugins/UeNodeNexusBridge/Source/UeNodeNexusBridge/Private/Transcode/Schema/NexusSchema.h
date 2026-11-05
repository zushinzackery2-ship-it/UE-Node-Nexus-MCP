#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;
class UClass;

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> SchemaEnvironment();
void AddClassMetadata(UClass* Class, const TSharedPtr<FJsonObject>& Record);
void AddPropertyMetadata(FProperty* Property, const TSharedPtr<FJsonObject>& Record, const UClass* OwnerClass = nullptr);
// Blueprint-callable and BlueprintEvent functions, with ``callable`` / ``event`` flags.
TSharedPtr<FJsonObject> FunctionIndex();
// Macro libraries' graph tunnel pins, keyed by ``<library path>.<macro>``.
TSharedPtr<FJsonObject> MacroIndex();
TSharedPtr<FJsonObject> CommonTypes();
bool LoadSchemaBlueprintClasses(FString& Error);
}
