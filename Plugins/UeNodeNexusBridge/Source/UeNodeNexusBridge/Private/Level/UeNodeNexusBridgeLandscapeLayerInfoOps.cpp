#include "UeNodeNexusBridgeLandscapeLayerInfoOps.h"

#include "LandscapeProxy.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleLandscapeLayerInfoSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyError;
    ALandscapeProxy* Landscape = ResolveLandscapeProxyForLayerInfo(Payload, Operation, RequestId, EarlyError);
    if (Landscape == nullptr)
    {
        return EarlyError;
    }

    const TArray<TSharedPtr<FJsonValue>>* LayerValues = nullptr;
    if (!Payload->TryGetArrayField(TEXT("layers"), LayerValues) || LayerValues == nullptr || LayerValues->Num() == 0)
    {
        return MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("invalid_request"), TEXT("layers must be a non-empty array"));
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    TArray<FLandscapeLayerInfoPlan> Plans;
    Plans.Reserve(LayerValues->Num());
    for (int32 Index = 0; Index < LayerValues->Num(); ++Index)
    {
        TSharedPtr<FJsonObject> LayerPayload = (*LayerValues)[Index].IsValid() ? (*LayerValues)[Index]->AsObject() : nullptr;
        if (!LayerPayload.IsValid())
        {
            return MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("invalid_layer_item"), TEXT("each layers item must be an object"));
        }

        FLandscapeLayerInfoPlan Plan;
        TSharedPtr<FJsonObject> PlanError;
        if (!BuildLandscapeLayerInfoPlan(Landscape, LayerPayload, Index, Operation, RequestId, Plan, PlanError))
        {
            return PlanError;
        }
        Plans.Add(Plan);
    }

    TArray<UPackage*> PackagesToSave;
    bool bApplied = false;
    bool bSaved = false;
    if (!bDryRun)
    {
        for (FLandscapeLayerInfoPlan& Plan : Plans)
        {
            if (!ApplyLandscapeLayerInfoPlan(Landscape, Plan, PackagesToSave))
            {
                return MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("layer_info_create_failed"), TEXT("LayerInfo asset could not be created"));
            }
        }
        RefreshLandscapeLayerInfoEditorState(Landscape);
        bApplied = true;
        bSaved = true;
        for (UPackage* Package : PackagesToSave)
        {
            FString SaveError;
            bSaved = (!bSave || Transcode::SavePackageDirect(Package, nullptr, SaveError)) && bSaved;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeWriteData(
        bDryRun,
        bApplied,
        AnyLandscapeLayerInfoPlanChanges(Plans),
        MakeLandscapeLayerInfoDiff(Plans, bApplied),
        MakePinIntegrity(true, {}, {}),
        MakeCompilePostCheck(false, false, true, 0, 0),
        MakeDirtyState(Landscape));
    Data->SetStringField(TEXT("actor_path"), Landscape->GetPathName());
    Data->SetObjectField(TEXT("save"), MakeLandscapeLayerInfoSavedPackagesJson(PackagesToSave, bSave && bSaved));

    const bool bOk = bDryRun || !bSave || bSaved;
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bOk)
    {
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("save_failed"), TEXT("LayerInfo binding was applied but one or more packages failed to save")));
    }
    return Response;
}
}
