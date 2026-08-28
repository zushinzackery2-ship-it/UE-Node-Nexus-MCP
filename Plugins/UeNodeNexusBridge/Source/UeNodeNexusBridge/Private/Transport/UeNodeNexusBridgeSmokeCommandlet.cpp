#include "UeNodeNexusBridgeSmokeCommandlet.h"

#include "Logging/LogMacros.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

DEFINE_LOG_CATEGORY_STATIC(LogUeNodeNexusBridgeSmoke, Log, All);

UUeNodeNexusBridgeSmokeCommandlet::UUeNodeNexusBridgeSmokeCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UUeNodeNexusBridgeSmokeCommandlet::Main(const FString& Params)
{
    FString RequestFile;
    FString ResponseFile;
    if (!FParse::Value(*Params, TEXT("RequestFile="), RequestFile) ||
        !FParse::Value(*Params, TEXT("ResponseFile="), ResponseFile))
    {
        UE_LOG(LogUeNodeNexusBridgeSmoke, Error, TEXT("Usage: -run=UeNodeNexusBridgeSmoke -RequestFile=<jsonl> -ResponseFile=<jsonl>"));
        return 1;
    }

    TArray<FString> RequestLines;
    if (!FFileHelper::LoadFileToStringArray(RequestLines, *RequestFile))
    {
        UE_LOG(LogUeNodeNexusBridgeSmoke, Error, TEXT("Could not read request file: %s"), *RequestFile);
        return 1;
    }

    // Commandlet Main already runs on the game thread, which is the same
    // contract the pipe transport fulfills before calling the dispatcher.
    TArray<FString> ResponseLines;
    int32 Dispatched = 0;
    for (const FString& Line : RequestLines)
    {
        if (Line.TrimStartAndEnd().IsEmpty())
        {
            continue;
        }
        ResponseLines.Add(UeNodeNexusBridge::DispatchBodyToResponseString(Line));
        ++Dispatched;
    }

    if (!FFileHelper::SaveStringArrayToFile(ResponseLines, *ResponseFile))
    {
        UE_LOG(LogUeNodeNexusBridgeSmoke, Error, TEXT("Could not write response file: %s"), *ResponseFile);
        return 1;
    }

    UE_LOG(LogUeNodeNexusBridgeSmoke, Display, TEXT("Dispatched %d request(s); responses written to %s"), Dispatched, *ResponseFile);
    return 0;
}
