#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperationRegistry.h"

namespace UeNodeNexusBridge
{
namespace
{
static void CountDiagnosticsBySeverity(const TArray<TSharedPtr<FJsonValue>>& Diagnostics, int32& OutErrorCount, int32& OutWarningCount)
{
    OutErrorCount = 0;
    OutWarningCount = 0;

    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject> Diagnostic = DiagnosticValue.IsValid() ? DiagnosticValue->AsObject() : nullptr;
        if (!Diagnostic.IsValid())
        {
            continue;
        }

        FString Severity;
        if (!Diagnostic->TryGetStringField(TEXT("severity"), Severity))
        {
            continue;
        }

        if (Severity.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Severity.Equals(TEXT("fatal"), ESearchCase::IgnoreCase))
        {
            ++OutErrorCount;
        }
        else if (Severity.Equals(TEXT("warning"), ESearchCase::IgnoreCase))
        {
            ++OutWarningCount;
        }
    }
}

static TSharedPtr<FJsonObject> UnsupportedOperation(const FString& Operation, const FString& RequestId)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("operation_not_implemented"), TEXT("Operation is known by the MCP contract but not implemented in this bridge build")));
    return Response;
}
}

TSharedPtr<FJsonObject> HandleDiagnosticsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const FBridgeDiagnosticsResult Diagnostics = CollectBridgeDiagnostics(Payload);
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    CountDiagnosticsBySeverity(Diagnostics.Diagnostics, ErrorCount, WarningCount);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("items"), Diagnostics.Diagnostics);
    Data->SetNumberField(TEXT("error_count"), ErrorCount);
    Data->SetNumberField(TEXT("warning_count"), WarningCount);
    Data->SetNumberField(TEXT("item_count"), Diagnostics.Diagnostics.Num());
    Data->SetStringField(TEXT("source"), TEXT("UeNodeNexusBridge"));
    Data->SetStringField(TEXT("scope"), Diagnostics.Scope);
    Data->SetNumberField(TEXT("assets_scanned"), Diagnostics.AssetsScanned);
    Data->SetNumberField(TEXT("assets_supported"), Diagnostics.AssetsSupported);
    Data->SetNumberField(TEXT("assets_unsupported"), Diagnostics.AssetsUnsupported);
    Data->SetNumberField(TEXT("assets_failed_to_load"), Diagnostics.AssetsFailedToLoad);
    Data->SetNumberField(TEXT("assets_not_loaded"), Diagnostics.AssetsNotLoaded);
    Data->SetStringField(TEXT("inspection_mode"), TEXT("loaded_objects_without_compilation"));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Diagnostics.bOk);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Diagnostics.bOk)
    {
        Response->SetObjectField(TEXT("error"), MakeError(Diagnostics.ErrorCode, Diagnostics.ErrorMessage));
    }
    return Response;
}

TSharedPtr<FJsonObject> DispatchOperation(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    // Core, AutoIndex, and plugin-contributed operations all dispatch through
    // the shared registry. The only fallback left is the machine-readable
    // unsupported-operation envelope.
    TSharedPtr<FJsonObject> RegisteredResponse;
    if (DispatchRegisteredOperation(Operation, RequestId, Payload, RegisteredResponse))
    {
        return RegisteredResponse;
    }

    return UnsupportedOperation(Operation, RequestId);
}
}
