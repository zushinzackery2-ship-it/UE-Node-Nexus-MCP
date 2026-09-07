#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> MakeApplyResponse(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Data,
    const FApplyContext& Context,
    const FBridgeAssetCompileDiagnostics& Compile,
    int32 PlanCount,
    bool bSaveRequired,
    bool bSaved,
    const FString& SaveError,
    const FString& SaveCode);
}
