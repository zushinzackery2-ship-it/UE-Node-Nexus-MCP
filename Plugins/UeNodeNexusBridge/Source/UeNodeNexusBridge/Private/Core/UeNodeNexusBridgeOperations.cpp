#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeDiagnostics.h"
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperationRegistry.h"

namespace UeNodeNexusBridge
{
namespace
{
static void CountDiagnosticsBySeverity(const TArray<TSharedPtr<FJsonValue>>& Diagnostics, int32& OutErrorCount, int32& OutWarningCount, bool bAssetsOnly = false)
{
    OutErrorCount = 0;
    OutWarningCount = 0;

    for (const TSharedPtr<FJsonValue>& DiagnosticValue : Diagnostics)
    {
        const TSharedPtr<FJsonObject> Diagnostic = DiagnosticValue.IsValid() ? DiagnosticValue->AsObject() : nullptr;
        if (!Diagnostic.IsValid() || (bAssetsOnly && Diagnostic->HasField(TEXT("session_id"))))
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
            double Count = 1;
            Diagnostic->TryGetNumberField(TEXT("occurrence_count"), Count);
            OutErrorCount += static_cast<int32>(Count);
        }
        else if (Severity.Equals(TEXT("warning"), ESearchCase::IgnoreCase))
        {
            double Count = 1;
            Diagnostic->TryGetNumberField(TEXT("occurrence_count"), Count);
            OutWarningCount += static_cast<int32>(Count);
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
    FString RequestError;
    if (!RuntimeDiagnostics::ValidateRequest(Payload, RequestError))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), RequestError);
    }
    const FBridgeDiagnosticsResult Diagnostics = CollectBridgeDiagnostics(Payload);
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    CountDiagnosticsBySeverity(Diagnostics.Diagnostics, ErrorCount, WarningCount);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("items"), Diagnostics.Diagnostics);
    Data->SetNumberField(TEXT("returned_error_count"), ErrorCount);
    Data->SetNumberField(TEXT("returned_warning_count"), WarningCount);
    CountDiagnosticsBySeverity(Diagnostics.Diagnostics, ErrorCount, WarningCount, true);
    FString Severity = TEXT("all");
    Payload->TryGetStringField(TEXT("severity"), Severity);
    if (Severity == TEXT("all") || Severity == TEXT("error"))
    {
        ErrorCount += Diagnostics.Runtime->GetIntegerField(TEXT("matched_error_count"));
    }
    if (Severity == TEXT("all") || Severity == TEXT("warning"))
    {
        WarningCount += Diagnostics.Runtime->GetIntegerField(TEXT("matched_warning_count"));
    }
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
    Data->SetStringField(TEXT("inspection_mode"), Diagnostics.bInspectAssets
        ? TEXT("loaded_objects_and_runtime_events") : TEXT("runtime_events"));
    Data->SetStringField(TEXT("coverage"), Diagnostics.AssetsUnsupported || Diagnostics.AssetsNotLoaded
        || Diagnostics.Runtime->GetBoolField(TEXT("cursor_gap")) || !Diagnostics.Runtime->GetBoolField(TEXT("asset_counts_complete"))
        || (Diagnostics.Scope != TEXT("project") && Diagnostics.Runtime->GetNumberField(TEXT("unattributed_error_count")) > 0)
        ? TEXT("partial") : TEXT("inspected_sources"));
    Data->SetObjectField(TEXT("runtime"), Diagnostics.Runtime);

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
