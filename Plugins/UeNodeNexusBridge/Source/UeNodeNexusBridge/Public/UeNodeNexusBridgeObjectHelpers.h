#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;

namespace UeNodeNexusBridge
{
UENODENEXUSBRIDGE_API UObject* ResolveObjectByPath(const FString& ObjectPath);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeVectorJson(const FVector& Value);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeRotatorJson(const FRotator& Value);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeTransformJson(const FTransform& Value);
UENODENEXUSBRIDGE_API FString PropertyValueToText(UObject* Object, FProperty* Property);
// Normalizes an exported-property string for compact summaries: drops the outer
// struct parens, flattens CR/LF to spaces, optionally strips quotes, and clamps
// to MaxLen (0 = no clamp).
UENODENEXUSBRIDGE_API FString CleanExportedPropertyText(const FString& Value, int32 MaxLen = 0, bool bStripQuotes = false);
// Exports a named property on Object to a cleaned display string. Object
// properties yield the referenced object's path name; others are exported text
// normalized via CleanExportedPropertyText(300). Returns false when the property
// does not exist on the object's class.
UENODENEXUSBRIDGE_API bool ExportNamedPropertyText(UObject* Object, const FName& PropertyName, FString& OutValue);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonValue> PropertyValueToJson(UObject* Object, FProperty* Property);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> PropertyToJson(UObject* Object, FProperty* Property, bool bFull);
UENODENEXUSBRIDGE_API bool ShouldExposeProperty(FProperty* Property, bool bIncludeNonEditable);
UENODENEXUSBRIDGE_API bool ApplyPropertyText(UObject* Object, FProperty* Property, const FString& ValueText);
UENODENEXUSBRIDGE_API bool JsonValueToPropertyImportText(FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
UENODENEXUSBRIDGE_API bool ApplyPropertyJsonValue(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
UENODENEXUSBRIDGE_API FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value);
UENODENEXUSBRIDGE_API bool SaveObjectConfig(UObject* Object, FString& OutConfigFile);
}
