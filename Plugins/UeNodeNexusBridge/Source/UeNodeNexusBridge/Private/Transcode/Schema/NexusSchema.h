#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> SchemaEnvironment();
void AddClassMetadata(UClass* Class, const TSharedPtr<FJsonObject>& Record);
void AddPropertyMetadata(FProperty* Property, const TSharedPtr<FJsonObject>& Record);
// Blueprint-callable and BlueprintEvent functions, with ``callable`` / ``event`` flags.
TSharedPtr<FJsonObject> FunctionIndex();
TSharedPtr<FJsonObject> CommonTypes();
bool LoadSchemaBlueprintClasses(FString& Error);
}
