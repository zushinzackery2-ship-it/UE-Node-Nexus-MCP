#include "UeNodeNexusBridgeOperations.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> DispatchAutoIndexOperation(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    if (Operation == TEXT("auto_index_enable"))
    {
        return HandleAutoIndexEnable(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_disable"))
    {
        return HandleAutoIndexDisable(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_status"))
    {
        return HandleAutoIndexStatus(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_rebuild"))
    {
        return HandleAutoIndexRebuild(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_flush"))
    {
        return HandleAutoIndexFlush(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_clear"))
    {
        return HandleAutoIndexClear(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_overview"))
    {
        return HandleAutoIndexOverview(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_tree_get"))
    {
        return HandleAutoIndexTreeGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_query"))
    {
        return HandleAutoIndexQuery(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_get"))
    {
        return HandleAutoIndexGet(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_resolve_path"))
    {
        return HandleAutoIndexResolvePath(Operation, RequestId, Payload);
    }
    if (Operation == TEXT("auto_index_diff_registry"))
    {
        return HandleAutoIndexDiffRegistry(Operation, RequestId, Payload);
    }

    return nullptr;
}
}
