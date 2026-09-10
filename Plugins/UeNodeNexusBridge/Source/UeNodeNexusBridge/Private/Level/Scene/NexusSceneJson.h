#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::Scene
{
using FObject = TSharedPtr<FJsonObject>;
using FRows = TArray<TSharedPtr<FJsonValue>>;
FString String(const FObject& Object, const TCHAR* Key, const FString& Default = FString());
FObject Object(const FObject& Json, const TCHAR* Key);
const FRows& Rows(const FObject& Json, const TCHAR* Key);
FString TransformText(const FTransform& Value);
bool ReadTransform(const FString& Text, FTransform& Value, FString& Error);
FString Digest(const FObject& Json);
bool ReadFile(const FString& File, FObject& Json, FString& Error);
}
