#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
struct FAssetWriteSave
{
    bool bAttempted = false;
    bool bSaved = false;
    bool bMemoryChanged = false;
    FString Error;
    FString Code;

    bool Failed() const
    {
        return bAttempted && !bSaved;
    }
};

UENODENEXUSBRIDGE_API FAssetWriteSave SaveAssetWrite(UObject* Asset, bool bRequested, bool bDryRun, bool bChanged);
UENODENEXUSBRIDGE_API void ApplyAssetWriteSave(const TSharedPtr<FJsonObject>& Response,
    const TSharedPtr<FJsonObject>& Data, const FAssetWriteSave& Save);
}
