#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "UeNodeNexusBridgeSmokeCommandlet.generated.h"

// Headless smoke driver. Replays a JSONL file of {operation, request_id,
// payload} envelopes through the exact dispatch path the named-pipe transport
// uses and writes one JSON response per line. This lets CI and non-Windows
// hosts (where the pipe server is a no-op) verify operation handlers inside a
// real editor process:
//
//   UnrealEditor-Cmd Host.uproject -run=UeNodeNexusBridgeSmoke
//       -RequestFile=/abs/requests.jsonl -ResponseFile=/abs/responses.jsonl
UCLASS()
class UUeNodeNexusBridgeSmokeCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UUeNodeNexusBridgeSmokeCommandlet();

    virtual int32 Main(const FString& Params) override;
};
