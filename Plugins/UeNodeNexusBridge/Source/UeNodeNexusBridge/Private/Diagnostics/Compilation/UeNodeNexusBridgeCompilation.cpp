#include "UeNodeNexusBridgeCompilation.h"

#include "HAL/PlatformTime.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "RenderCommandFence.h"
#include "RHI.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge
{
void FinishRenderingUpdates()
{
    const double Started = FPlatformTime::Seconds();
    FRenderCommandFence Fence;
    Fence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
    Fence.Wait(false);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=rhi_fence duration_ms=%.3f"),
        *ActiveBridgeRequestId(), (FPlatformTime::Seconds() - Started) * 1000.0);
}

FBridgeAssetCompileDiagnostics MaterialResourceStatus(UMaterialInterface* Material, bool bWait, bool bRenderFence)
{
    const double Started = FPlatformTime::Seconds();
    FBridgeAssetCompileDiagnostics Result;
    Result.bSupported = true;
    Result.bRan = bWait;
    Result.State = bWait ? TEXT("submitted") : TEXT("pending");
    TSet<UMaterialInterface*> Ancestors;
    UMaterialInterface* Current = Material;
    while (Current)
    {
        if (Ancestors.Contains(Current))
        {
            Result.State = TEXT("failed");
            Result.ErrorCount = 1;
            Result.Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("material_parent_cycle"),
                TEXT("Material instance parent chain contains a cycle"), Material->GetPathName(), TEXT("Nexus"))));
            return Result;
        }
        Ancestors.Add(Current);
        UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
        Current = Instance ? Instance->Parent : nullptr;
    }
    FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIFeatureLevel) : nullptr;
    if (Material)
    {
        Result.AssetClass = Material->GetClass()->GetPathName();
    }
    if (Resource && bWait)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=compile_submitted asset=%s"), *ActiveBridgeRequestId(), *Material->GetPathName());
        Resource->FinishCompilation();
    }
    if (Resource)
    {
        AddMaterialCompileDiagnostics(Resource->GetCompileErrors(), Material->GetPathName(), Result.Diagnostics);
        Result.ErrorCount = Resource->GetCompileErrors().Num();
        Result.bShaderReady = Resource->IsCompilationFinished() && Resource->IsGameThreadShaderMapComplete();
    }
    if (Result.bShaderReady && bWait && bRenderFence)
    {
        FinishRenderingUpdates();
        Result.bRenderReady = true;
    }
    Result.bOk = Result.bShaderReady && (!bWait || !bRenderFence || Result.bRenderReady) && Result.ErrorCount == 0;
    Result.State = Result.ErrorCount > 0 ? TEXT("failed") : (Result.bOk && Result.bRenderReady ? TEXT("ready") : TEXT("pending"));
    if (bWait && !Result.bOk && Result.ErrorCount == 0)
    {
        Result.State = TEXT("failed");
        Result.ErrorCount = 1;
        Result.Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(
            TEXT("error"), TEXT("material_resource_not_ready"), TEXT("Target shader map is not complete after compilation"),
            Material ? Material->GetPathName() : FString(), TEXT("Unreal.MaterialCompiler"))));
    }
    Result.DurationMs = (FPlatformTime::Seconds() - Started) * 1000.0;
    if (bWait)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=compile_%s asset=%s duration_ms=%.3f errors=%d"),
            *ActiveBridgeRequestId(), *Result.State, Material ? *Material->GetPathName() : TEXT("null"), Result.DurationMs, Result.ErrorCount);
    }
    return Result;
}

TSharedPtr<FJsonObject> CompileDiagnosticsJson(const FBridgeAssetCompileDiagnostics& Result, bool bRequested)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("requested"), bRequested);
    Json->SetBoolField(TEXT("supported"), Result.bSupported);
    Json->SetBoolField(TEXT("ran"), Result.bRan);
    Json->SetBoolField(TEXT("ok"), Result.bOk);
    Json->SetStringField(TEXT("state"), Result.State);
    Json->SetBoolField(TEXT("shader_ready"), Result.bShaderReady);
    Json->SetBoolField(TEXT("render_ready"), Result.bRenderReady);
    Json->SetNumberField(TEXT("error_count"), Result.ErrorCount);
    Json->SetNumberField(TEXT("warning_count"), Result.WarningCount);
    Json->SetNumberField(TEXT("duration_ms"), Result.DurationMs);
    Json->SetStringField(TEXT("readiness_scope"), TEXT("target_resources_rhi_submission"));
    return Json;
}
}
