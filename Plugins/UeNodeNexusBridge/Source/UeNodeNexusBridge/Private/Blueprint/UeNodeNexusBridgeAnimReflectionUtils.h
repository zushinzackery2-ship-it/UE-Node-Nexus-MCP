#pragma once

#include "CoreMinimal.h"

class FProperty;
class UStruct;

namespace UeNodeNexusBridge
{
// Reflection helpers shared by the AnimBlueprint summary and the state-machine
// summary ops to dump AnimNode_* / transition struct fields into compact
// "alias=value" parts without linking the AnimGraph editor types.

// Exports one property on a struct/object container to a cleaned display string.
// Object properties yield the referenced object's path; others use exported text
// normalized via CleanExportedPropertyText(160, stripQuotes=true). Returns false
// (leaving OutValue untouched of meaning) when missing/empty/"None".
bool ExportAnimFieldValue(UObject* Owner, const void* Container, FProperty* Property, FString& OutValue);

// Case-insensitive property lookup over a struct, including super structs.
FProperty* FindAnimPropertyCaseInsensitive(UStruct* Struct, const FString& Name);

// Appends "Alias=Value" to Parts when the named field on Struct exports a value.
void AddAnimField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& FieldName, const FString& Alias);

// Appends "Alias=Value" for a field nested inside a struct field on Struct.
void AddAnimStructField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& StructFieldName, const FString& InnerFieldName, const FString& Alias);
}
