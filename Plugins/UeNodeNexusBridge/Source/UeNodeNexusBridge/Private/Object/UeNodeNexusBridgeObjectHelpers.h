#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class FProperty;

namespace UeNodeNexusBridge
{
UObject* ResolveObjectByPath(const FString& ObjectPath);
TSharedPtr<FJsonObject> MakeVectorJson(const FVector& Value);
TSharedPtr<FJsonObject> MakeRotatorJson(const FRotator& Value);
TSharedPtr<FJsonObject> MakeTransformJson(const FTransform& Value);
FString PropertyValueToText(UObject* Object, FProperty* Property);
TSharedPtr<FJsonValue> PropertyValueToJson(UObject* Object, FProperty* Property);
TSharedPtr<FJsonObject> PropertyToJson(UObject* Object, FProperty* Property, bool bFull);
bool ShouldExposeProperty(FProperty* Property, bool bIncludeNonEditable);
bool ApplyPropertyText(UObject* Object, FProperty* Property, const FString& ValueText);
bool JsonValueToPropertyImportText(FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
bool ApplyPropertyJsonValue(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value);
bool SaveObjectConfig(UObject* Object, FString& OutConfigFile);
}
