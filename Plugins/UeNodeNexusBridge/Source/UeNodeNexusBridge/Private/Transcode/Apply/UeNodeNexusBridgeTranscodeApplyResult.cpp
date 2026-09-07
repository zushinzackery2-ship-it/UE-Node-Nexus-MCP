#include "UeNodeNexusBridgeTranscodeApplyResult.h"

#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> MakeApplyResponse(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Data,
    const FApplyContext& Context,
    const FBridgeAssetCompileDiagnostics& Compile,
    int32 PlanCount,
    bool bSaveRequired,
    bool bSaved,
    const FString& SaveError,
    const FString& SaveCode)
{
    const bool bCompileSucceeded = !Compile.bRan || Compile.bOk;
    const bool bApplySucceeded = Context.Failures.IsEmpty()
        && bCompileSucceeded && (!bSaveRequired || bSaved);
    const int32 SaveErrorCount = bSaveRequired && !bSaved ? 1 : 0;
    Data->SetNumberField(
        TEXT("remaining_errors"),
        FMath::Max(Compile.ErrorCount, bCompileSucceeded ? 0 : 1)
            + Context.Failures.Num() + SaveErrorCount);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bApplySucceeded);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Context.Failures.IsEmpty())
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(
            FString(TEXT("plan_partially_failed")),
            FString::Printf(TEXT("%d of %d plan verbs failed; first: %s"),
                Context.Failures.Num(), PlanCount, *Context.Failures[0].Message)));
    }
    else if (bSaveRequired && !bSaved)
    {
        const FString Code = SaveCode.IsEmpty() ? FString(TEXT("save_failed")) : SaveCode;
        const FString Message = SaveError.IsEmpty()
            ? FString(TEXT("asset changed but package save failed")) : SaveError;
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, Message));
    }
    else if (!bCompileSucceeded)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(
            FString(TEXT("compile_failed")), FString(TEXT("asset compile reported errors"))));
    }
    return Response;
}
}
