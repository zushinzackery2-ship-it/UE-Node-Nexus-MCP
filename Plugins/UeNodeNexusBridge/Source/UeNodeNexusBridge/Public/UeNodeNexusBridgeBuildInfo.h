#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
struct FBridgeBuildIdentity
{
    FString Version;
    FString SourceCommit;
    FString SourceFingerprint;
    bool bSourceDirty = true;
    bool bRecorded = false;
    int32 ContractVersion = 0;
};

UENODENEXUSBRIDGE_API void RegisterBuildIdentity(FName Module, const FBridgeBuildIdentity& Identity);
UENODENEXUSBRIDGE_API void UnregisterBuildIdentity(FName Module);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> BridgeBuildIdentities();
}
