#include "NexusViewportTemporalProbe.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "SceneTextures.h"

namespace UeNodeNexusBridge::Diagnostics
{
namespace
{
// Read the engine's rolling history layout (TemporalSuperResolution.cpp, UE5.5).
int32 SliceIndex(const FTSRHistory& History, int32 RollingIndex)
{
    const int32 Count = History.FrameStorageCount;
    const int32 Period = History.FrameStoragePeriod;
    if (Count == 1)
    {
        return 0;
    }
    if (Period == 1)
    {
        return (RollingIndex % 2) * (Count / 2) + (RollingIndex / 2) % (Count / 2);
    }
    if (RollingIndex % Period == 0)
    {
        const int32 PersistentIndex = RollingIndex / Period;
        return PersistentIndex % 2 ? Count / 2 + PersistentIndex / 2
            : Count / 2 - PersistentIndex / 2 - 1;
    }
    return RollingIndex % 2 ? Count - 1 : 0;
}

int32 ResurrectionIndex(const FTSRHistory& History)
{
    const int32 Count = History.FrameStorageCount;
    const int32 Period = History.FrameStoragePeriod;
    if (Count == 1)
    {
        return 0;
    }
    if (Period == 1)
    {
        return (Count + History.LastFrameRollingIndex
            - FMath::DivideAndRoundUp(FMath::Max(History.AccumulatedFrameCount - 2, 0), 2) * 2) % Count;
    }
    const int32 RollingCount = (Count - 2) * Period;
    return History.AccumulatedFrameCount < RollingCount ? 0
        : FMath::DivideAndRoundUp(History.LastFrameRollingIndex + Period, Period) * Period % RollingCount;
}

void CopyHistory(FRDGBuilder& GraphBuilder, const FTSRHistory& History, FIntRect Rect,
    const TCHAR* Prefix, uint32 Slice, FTemporalProbeCopy Copy)
{
    const FVector2d Scale(double(History.OutputViewportRect.Width()) / History.InputViewportRect.Width(),
        double(History.OutputViewportRect.Height()) / History.InputViewportRect.Height());
    const auto Map = [&History, Scale](FIntPoint Point)
    {
        const FIntPoint Local = Point - History.InputViewportRect.Min;
        return History.OutputViewportRect.Min + FIntPoint(
            FMath::RoundToInt(Local.X * Scale.X), FMath::RoundToInt(Local.Y * Scale.Y));
    };
    const FIntRect HistoryRect(Map(Rect.Min), Map(Rect.Max));
    const uint32 GuideStride = History.GuideArray->GetDesc().ArraySize / History.FrameStorageCount;
    Copy(*(FString(Prefix) + TEXT("_guide")), GraphBuilder.RegisterExternalTexture(History.GuideArray), Rect, Slice * GuideStride);
    if (GuideStride == 2)
    {
        Copy(*(FString(Prefix) + TEXT("_guide_metadata")), GraphBuilder.RegisterExternalTexture(History.GuideArray), Rect, Slice * GuideStride + 1);
    }
    const uint32 ColorStride = History.ColorArray->GetDesc().ArraySize / History.FrameStorageCount;
    Copy(*(FString(Prefix) + TEXT("_color")), GraphBuilder.RegisterExternalTexture(History.ColorArray), HistoryRect, Slice * ColorStride);
    Copy(*(FString(Prefix) + TEXT("_metadata")), GraphBuilder.RegisterExternalTexture(History.MetadataArray), HistoryRect, Slice);
    if (History.MoireArray.IsValid())
    {
        Copy(*(FString(Prefix) + TEXT("_moire")), GraphBuilder.RegisterExternalTexture(History.MoireArray), Rect, 0);
    }
}
}

void ObserveViewportTemporalInputs(FRDGBuilder& GraphBuilder, const FSceneView& InView,
    FJsonObject& Result, FIntRect Rect, FTemporalProbeCopy Copy)
{
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    const FRDGTextureRef NativeVelocity = View.GetSceneTextures().Velocity;
    Result.SetBoolField(TEXT("native_velocity_produced"), HasBeenProduced(NativeVelocity));
    if (NativeVelocity)
    {
        Result.SetNumberField(TEXT("native_velocity_width"), NativeVelocity->Desc.Extent.X);
        Result.SetNumberField(TEXT("native_velocity_height"), NativeVelocity->Desc.Extent.Y);
        Result.SetStringField(TEXT("native_velocity_format"), GPixelFormats[NativeVelocity->Desc.Format].Name);
    }
    Result.SetBoolField(TEXT("camera_cut"), View.bCameraCut);
    Result.SetBoolField(TEXT("previous_transforms_reset"), View.bPrevTransformsReset);
    Result.SetBoolField(TEXT("realtime_update"), View.Family->bRealtimeUpdate);
    Result.SetBoolField(TEXT("motion_blur_show_flag"), View.Family->EngineShowFlags.MotionBlur);
    Result.SetNumberField(TEXT("motion_blur_amount"), View.FinalPostProcessSettings.MotionBlurAmount);
    Result.SetNumberField(TEXT("motion_blur_max"), View.FinalPostProcessSettings.MotionBlurMax);
    Result.SetNumberField(TEXT("motion_blur_quality"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt());
    Result.SetNumberField(TEXT("delta_real_time"), View.Family->Time.GetDeltaRealTimeSeconds());
    Result.SetNumberField(TEXT("anti_aliasing_method"), View.AntiAliasingMethod);
    const FTSRHistory& History = View.PrevViewInfo.TSRHistory;
    Result.SetBoolField(TEXT("tsr_input_history_valid"), History.IsValid());
    Result.SetNumberField(TEXT("tsr_storage_count"), History.FrameStorageCount);
    Result.SetNumberField(TEXT("tsr_storage_period"), History.FrameStoragePeriod);
    Result.SetNumberField(TEXT("tsr_accumulated_frames"), History.AccumulatedFrameCount);
    Result.SetNumberField(TEXT("tsr_last_rolling_index"), History.LastFrameRollingIndex);
    if (History.IsValid())
    {
        const int32 LatestSlice = SliceIndex(History, History.LastFrameRollingIndex);
        const int32 ResurrectedSlice = SliceIndex(History, ResurrectionIndex(History));
        Result.SetNumberField(TEXT("tsr_latest_slice"), LatestSlice);
        Result.SetNumberField(TEXT("tsr_resurrected_slice"), ResurrectedSlice);
        CopyHistory(GraphBuilder, History, Rect, TEXT("tsr_latest"), LatestSlice, Copy);
        CopyHistory(GraphBuilder, History, Rect, TEXT("tsr_resurrected"), ResurrectedSlice, Copy);
    }
}
}
