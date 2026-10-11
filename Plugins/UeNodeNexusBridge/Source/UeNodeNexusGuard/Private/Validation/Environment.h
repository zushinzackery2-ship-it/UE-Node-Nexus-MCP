#pragma once

#include "NexusValidation.h"

namespace NexusLifecycle::Validation
{
FString Normalize(const FString& Value);
FString Expand(const FString& Value);
TSharedPtr<FJsonObject> Configuration();
bool ValidateConfiguration(const TSharedPtr<FJsonObject>& Input, FString& Error);
void ApplyConfiguration(const TSharedPtr<FJsonObject>& Input);
TSharedPtr<FJsonObject> ConsoleVariables();
bool ValidateConsoleVariables(const TSharedPtr<FJsonObject>& Input, FString& Error);
TArray<FString> ApplyConsoleVariables(const TSharedPtr<FJsonObject>& Input);
bool CompleteConsoleVariables(const TSharedPtr<FJsonObject>& Input, const TArray<FString>& Pending, FString& Error);
TSharedPtr<FJsonObject> RenderingLaunch();
}
