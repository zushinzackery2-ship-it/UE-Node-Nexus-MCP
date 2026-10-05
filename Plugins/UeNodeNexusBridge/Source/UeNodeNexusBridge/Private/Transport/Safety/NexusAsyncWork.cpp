#include "NexusAsyncWork.h"

#include "NexusLifecycle.h"
#include "RenderAssetUpdate.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge::Safety
{
static TMap<FString, FString> Work;

FString EngineBusyReason()
{
    if (UE::IsSavingPackage())
    {
        return TEXT("package_save");
    }
    if (IsGarbageCollecting())
    {
        return TEXT("garbage_collection");
    }
    if (IsAssetStreamingSuspended())
    {
        return TEXT("streaming_suspended");
    }
    return FString();
}

FString AsyncBusyReason()
{
    return Work.IsEmpty() ? FString() : TEXT("async_operation");
}

bool BeginAsync(const FString& RequestId, const FString& Operation)
{
    if (!IsInGameThread() || RequestId.IsEmpty() || !Work.IsEmpty())
    {
        return false;
    }
    Work.Add(RequestId, Operation);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=async_begin"), *RequestId, *Operation);
    return true;
}

bool Holds(const FString& RequestId)
{
    return Work.Contains(RequestId);
}

void FinishAsync(const FString& RequestId, bool bOk, const FString& Code)
{
    if (Work.Remove(RequestId) != 0)
    {
        NexusLifecycle::Complete(RequestId, bOk, Code, false);
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=async_end ok=%d code=%s"), *RequestId, bOk, *Code);
    }
}

TSharedPtr<FJsonObject> AsyncSnapshot()
{
    auto Result = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Pair : Work)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("request_id"), Pair.Key);
        Row->SetStringField(TEXT("operation"), Pair.Value);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Result->SetArrayField(TEXT("operations"), Rows);
    return Result;
}
}
