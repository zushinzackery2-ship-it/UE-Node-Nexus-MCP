#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeOperationRegistry.h"

namespace UeNodeNexusBridge
{
namespace
{
void RegisterAutoIndexOp(const TCHAR* Operation, FBridgeOperationHandler Handler)
{
    RegisterOperationHandler(FString(Operation), MoveTemp(Handler));
}
}

TArray<FString> GetAutoIndexOperationNames()
{
    return {
        TEXT("auto_index_enable"),
        TEXT("auto_index_disable"),
        TEXT("auto_index_status"),
        TEXT("auto_index_rebuild"),
        TEXT("auto_index_flush"),
        TEXT("auto_index_clear"),
        TEXT("auto_index_overview"),
        TEXT("auto_index_tree_get"),
        TEXT("auto_index_query"),
        TEXT("auto_index_get"),
        TEXT("auto_index_resolve_path"),
        TEXT("auto_index_diff_registry"),
    };
}

void RegisterAutoIndexOperations()
{
    RegisterAutoIndexOp(TEXT("auto_index_enable"), HandleAutoIndexEnable);
    RegisterAutoIndexOp(TEXT("auto_index_disable"), HandleAutoIndexDisable);
    RegisterAutoIndexOp(TEXT("auto_index_status"), HandleAutoIndexStatus);
    RegisterAutoIndexOp(TEXT("auto_index_rebuild"), HandleAutoIndexRebuild);
    RegisterAutoIndexOp(TEXT("auto_index_flush"), HandleAutoIndexFlush);
    RegisterAutoIndexOp(TEXT("auto_index_clear"), HandleAutoIndexClear);
    RegisterAutoIndexOp(TEXT("auto_index_overview"), HandleAutoIndexOverview);
    RegisterAutoIndexOp(TEXT("auto_index_tree_get"), HandleAutoIndexTreeGet);
    RegisterAutoIndexOp(TEXT("auto_index_query"), HandleAutoIndexQuery);
    RegisterAutoIndexOp(TEXT("auto_index_get"), HandleAutoIndexGet);
    RegisterAutoIndexOp(TEXT("auto_index_resolve_path"), HandleAutoIndexResolvePath);
    RegisterAutoIndexOp(TEXT("auto_index_diff_registry"), HandleAutoIndexDiffRegistry);

    for (const FString& Name : GetAutoIndexOperationNames())
    {
        ensureMsgf(IsOperationRegistered(Name), TEXT("AutoIndex operation %s has no registered handler"), *Name);
    }
}

void UnregisterAutoIndexOperations()
{
    for (const FString& Name : GetAutoIndexOperationNames())
    {
        UnregisterOperationHandler(Name);
    }
}
}
