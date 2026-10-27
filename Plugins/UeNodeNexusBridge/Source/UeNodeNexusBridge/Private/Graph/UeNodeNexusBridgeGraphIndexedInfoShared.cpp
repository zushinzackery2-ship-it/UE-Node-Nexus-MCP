#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"

#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
bool WantsRealIds(const TSharedPtr<FJsonObject>& Payload)
{
    FString IdMode = TEXT("alias");
    Payload->TryGetStringField(TEXT("id_mode"), IdMode);
    return IdMode.Equals(TEXT("real"), ESearchCase::IgnoreCase)
        || IdMode.Equals(TEXT("guid"), ESearchCase::IgnoreCase)
        || IdMode.Equals(TEXT("both"), ESearchCase::IgnoreCase);
}

int32 ReadIndexedMaxNodes(const TSharedPtr<FJsonObject>& Payload)
{
    double MaxNodes = 0.0;
    return Payload->TryGetNumberField(TEXT("max_nodes"), MaxNodes) ? FMath::Max(0, static_cast<int32>(MaxNodes)) : 0;
}
}
