#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle
{
FString CanonicalPath(const FString& Path, bool bDirectory = false);
FString Sha256(const FString& Text);
FString DefaultRuntime();
TSharedPtr<FJsonObject> ProcessIdentity(uint32 Pid);
bool SameProcess(const TSharedPtr<FJsonObject>& Identity);
bool WriteAtomic(const FString& Path, const TSharedPtr<FJsonObject>& Object);
TSharedPtr<FJsonObject> ReadObject(const FString& Path);
FString Encode(const TSharedPtr<FJsonObject>& Object);
bool InitializeIdentity();
void ReleaseIdentity();
}
