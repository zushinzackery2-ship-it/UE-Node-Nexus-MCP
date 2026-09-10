#include "UeNodeNexusBridgeCompilation.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> CompileAssetWrite(UObject* Asset, bool bRequested, bool bRun, TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    FBridgeAssetCompileDiagnostics Result;
    if (bRun)
    {
        Result = CollectAssetCompileDiagnostics(Asset, Asset->GetPathName(), true);
        Diagnostics.Append(Result.Diagnostics);
    }
    return CompileDiagnosticsJson(Result, bRequested);
}
}
