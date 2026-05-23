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
UENODENEXUSBRIDGE_API TSharedPtr<FJsonValue> PropertyValueToJson(UObject* Object, FProperty* Property);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> PropertyToJson(UObject* Object, FProperty* Property, bool bFull);
UENODENEXUSBRIDGE_API bool ShouldExposeProperty(FProperty* Property, bool bIncludeNonEditable);
UENODENEXUSBRIDGE_API bool ApplyPropertyText(UObject* Object, FProperty* Property, const FString& ValueText);
UENODENEXUSBRIDGE_API bool JsonValueToPropertyImportText(FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
UENODENEXUSBRIDGE_API bool ApplyPropertyJsonValue(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
UENODENEXUSBRIDGE_API FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value);
UENODENEXUSBRIDGE_API bool SaveObjectConfig(UObject* Object, FString& OutConfigFile);
}
