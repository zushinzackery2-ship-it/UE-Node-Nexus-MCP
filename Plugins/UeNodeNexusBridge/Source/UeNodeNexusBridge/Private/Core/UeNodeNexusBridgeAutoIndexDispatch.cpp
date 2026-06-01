#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeOperationRegistry.h"

namespace UeNodeNexusBridge
{
namespace
{
void Register(const TCHAR* Operation, FBridgeOperationHandler Handler)
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
    Register(TEXT("auto_index_enable"), HandleAutoIndexEnable);
    Register(TEXT("auto_index_disable"), HandleAutoIndexDisable);
    Register(TEXT("auto_index_status"), HandleAutoIndexStatus);
    Register(TEXT("auto_index_rebuild"), HandleAutoIndexRebuild);
    Register(TEXT("auto_index_flush"), HandleAutoIndexFlush);
    Register(TEXT("auto_index_clear"), HandleAutoIndexClear);
    Register(TEXT("auto_index_overview"), HandleAutoIndexOverview);
    Register(TEXT("auto_index_tree_get"), HandleAutoIndexTreeGet);
    Register(TEXT("auto_index_query"), HandleAutoIndexQuery);
    Register(TEXT("auto_index_get"), HandleAutoIndexGet);
    Register(TEXT("auto_index_resolve_path"), HandleAutoIndexResolvePath);
    Register(TEXT("auto_index_diff_registry"), HandleAutoIndexDiffRegistry);

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
