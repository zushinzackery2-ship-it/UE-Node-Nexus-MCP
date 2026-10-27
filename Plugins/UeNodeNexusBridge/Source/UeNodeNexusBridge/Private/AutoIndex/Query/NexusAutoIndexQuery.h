#pragma once

#include "CoreMinimal.h"

class FJsonObject;

// Read models over FState::Get(); each returns the operation's data object with its
// indexed text payload.
namespace UeNodeNexusBridge::AutoIndex
{
TSharedPtr<FJsonObject> BuildStatusData(const FString& Format);
TSharedPtr<FJsonObject> BuildOverviewData(const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> BuildTreeData(const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> BuildQueryData(const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> BuildResolveData(const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> BuildDiffData();
}
