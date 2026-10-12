#include "NexusViewportPixelProbe.h"
#include "NexusViewportTemporalProbe.h"
#include "PixelProbe/NexusPixelProbeState.h"

#include "Dom/JsonObject.h"
#include "PostProcess/PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SceneRendering.h"
#include "SceneManagement.h"
#include "SceneTexturesConfig.h"
#include "ScreenPass.h"

namespace UeNodeNexusBridge::Diagnostics
{
namespace
{
BEGIN_SHADER_PARAMETER_STRUCT(FProbeCopyParameters, )
    RDG_TEXTURE_ACCESS(SourceTexture, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

void SetMatrix(const TCHAR* Name, const FMatrix44f& Matrix)
{
    FProbeRequest& Probe = PixelProbeState_RenderThread();
    TArray<TSharedPtr<FJsonValue>> Values;
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Column = 0; Column < 4; ++Column)
        {
            Values.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Column]));
        }
    }
    Probe.Result->SetArrayField(Name, Values);
}

void CopyTexture(FRDGBuilder& GraphBuilder, const TCHAR* Name, FRDGTextureRef Texture,
    FIntRect Rect = FIntRect(), uint32 Slice = 0, FIntPoint ReportOffset = FIntPoint::ZeroValue)
{
    FProbeRequest& Probe = PixelProbeState_RenderThread();
    Rect = Rect.IsEmpty() ? Probe.Rect : Rect;
    if (!Texture || Texture->Desc.Extent.X < Rect.Max.X || Texture->Desc.Extent.Y < Rect.Max.Y
        || Slice >= Texture->Desc.ArraySize)
    {
        Probe.Result->SetBoolField(FString(Name) + TEXT("_available"), false);
        Probe.Result->SetStringField(FString(Name) + TEXT("_unavailable_reason"),
            !Texture ? TEXT("texture_missing") : TEXT("outside_texture_or_array"));
        if (Texture)
        {
            Probe.Result->SetNumberField(FString(Name) + TEXT("_source_width"), Texture->Desc.Extent.X);
            Probe.Result->SetNumberField(FString(Name) + TEXT("_source_height"), Texture->Desc.Extent.Y);
            Probe.Result->SetStringField(FString(Name) + TEXT("_source_format"), GPixelFormats[Texture->Desc.Format].Name);
        }
        return;
    }
    const EPixelFormat Format = Texture->Desc.Format;
    if (GPixelFormats[Format].BlockSizeX != 1 || GPixelFormats[Format].BlockSizeY != 1)
    {
        Probe.Result->SetBoolField(FString(Name) + TEXT("_available"), false);
        Probe.Result->SetStringField(FString(Name) + TEXT("_unavailable_reason"), TEXT("unsupported_block_format"));
        return;
    }
    FProbeTexture& Entry = Probe.Textures.AddDefaulted_GetRef();
    Entry.Name = Name;
    Entry.Format = Format;
    Entry.Rect = Rect + ReportOffset;
    Entry.Readback = MakeUnique<FRHIGPUTextureReadback>(FName(Name));
    auto* Parameters = GraphBuilder.AllocParameters<FProbeCopyParameters>();
    Parameters->SourceTexture = Texture;
    GraphBuilder.AddPass(RDG_EVENT_NAME("NexusPixelProbeCopy %s", Name), Parameters, ERDGPassFlags::Readback,
        [Readback = Entry.Readback.Get(), Texture, Rect, Slice](FRHICommandList& RHICmdList)
        {
            Readback->EnqueueCopy(RHICmdList, Texture->GetRHI(), FIntVector(Rect.Min.X, Rect.Min.Y, 0),
                Slice, FIntVector(Rect.Width(), Rect.Height(), 1));
        });
    Probe.Result->SetBoolField(FString(Name) + TEXT("_available"), true);
}

}

void ObserveViewportPixels_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
    const FPostProcessingInputs& Inputs)
{
    check(IsInRenderingThread());
    FProbeRequest& Probe = PixelProbeState_RenderThread();
    ExpirePixelProbe_RenderThread(Probe);
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    if (!Probe.bQueued || View.bIsSceneCapture || !View.State || View.State->GetViewKey() != Probe.ViewKey || !View.CachedViewUniformShaderParameters)
    {
        return;
    }
    const FViewUniformShaderParameters& Parameters = *View.CachedViewUniformShaderParameters;
    if (static_cast<int32>(Parameters.TemporalAAParams.X) != Probe.TemporalSample
        || Parameters.StateFrameIndex % 8 != static_cast<uint32>(Probe.FrameMod8))
    {
        return;
    }
    Probe.Result->SetStringField(TEXT("state"), TEXT("reading"));
    Probe.Result->SetStringField(TEXT("stage"), TEXT("before_post_processing"));
    Probe.Result->SetNumberField(TEXT("engine_frame"), Parameters.FrameCounter);
    Probe.Result->SetNumberField(TEXT("shader_frame_index"), Parameters.StateFrameIndex);
    Probe.Result->SetNumberField(TEXT("output_frame_index"), Parameters.StateOutputFrameIndex);
    Probe.Result->SetNumberField(TEXT("view_key"), View.State->GetViewKey());
    Probe.Result->SetNumberField(TEXT("x"), Probe.Rect.Min.X);
    Probe.Result->SetNumberField(TEXT("y"), Probe.Rect.Min.Y);
    Probe.Result->SetNumberField(TEXT("width"), Probe.Rect.Width());
    Probe.Result->SetNumberField(TEXT("height"), Probe.Rect.Height());
    SetMatrix(TEXT("world_to_view"), Parameters.TranslatedWorldToView);
    SetMatrix(TEXT("view_to_world"), Parameters.ViewToTranslatedWorld);
    SetMatrix(TEXT("view_to_clip"), Parameters.ViewToClip);
    CopyTexture(GraphBuilder, TEXT("scene_color"), (*Inputs.SceneTextures)->SceneColorTexture);
    CopyTexture(GraphBuilder, TEXT("normal"), (*Inputs.SceneTextures)->GBufferATexture);
    CopyTexture(GraphBuilder, TEXT("velocity"), (*Inputs.SceneTextures)->GBufferVelocityTexture);
    // D3D12 cannot create the PF_DepthStencil CPU staging footprint. Sample the
    // native depth SRV into a small R32F target without changing DeviceZ.
    FRDGTextureRef DepthReadback = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
        Probe.Rect.Size(), PF_R32_FLOAT, FClearValueBinding::None,
        TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("NexusProbe.DeviceZ"));
    AddDrawTexturePass(GraphBuilder, View, (*Inputs.SceneTextures)->SceneDepthTexture, DepthReadback,
        Probe.Rect.Min, FIntPoint::ZeroValue, Probe.Rect.Size());
    CopyTexture(GraphBuilder, TEXT("scene_depth"), DepthReadback,
        FIntRect(FIntPoint::ZeroValue, Probe.Rect.Size()), 0, Probe.Rect.Min);
    ObserveViewportTemporalInputs(GraphBuilder, View, *Probe.Result, Probe.Rect,
        [&GraphBuilder](const TCHAR* Name, FRDGTextureRef Texture, FIntRect Rect, uint32 Slice)
        {
            CopyTexture(GraphBuilder, Name, Texture, Rect, Slice);
        });
    Probe.bQueued = false;
    Probe.bReading = true;
}

bool HasViewportPixelProbe_RenderThread(const FSceneView& InView)
{
    check(IsInRenderingThread());
    FProbeRequest& Probe = PixelProbeState_RenderThread();
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    if ((!Probe.bQueued && !Probe.bReading) || View.bIsSceneCapture || !View.State || View.State->GetViewKey() != Probe.ViewKey
        || !View.CachedViewUniformShaderParameters)
    {
        return false;
    }
    const FViewUniformShaderParameters& Parameters = *View.CachedViewUniformShaderParameters;
    return static_cast<int32>(Parameters.TemporalAAParams.X) == Probe.TemporalSample
        && Parameters.StateFrameIndex % 8 == static_cast<uint32>(Probe.FrameMod8);
}

FScreenPassTexture ObserveViewportPostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
    const FPostProcessMaterialInputs& Inputs, const TCHAR* Stage)
{
    const FScreenPassTexture Color = FScreenPassTexture::CopyFromSlice(GraphBuilder,
        Inputs.GetInput(EPostProcessMaterialInput::SceneColor), Inputs.OverrideOutput);
    FProbeRequest& Probe = PixelProbeState_RenderThread();
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    if (Probe.bReading && View.State && View.State->GetViewKey() == Probe.ViewKey
        && View.CachedViewUniformShaderParameters
        && Probe.Result->GetNumberField(TEXT("engine_frame")) == View.CachedViewUniformShaderParameters->FrameCounter)
    {
        CopyTexture(GraphBuilder, Stage, Color.Texture);
        TArray<TSharedPtr<FJsonValue>> Rect;
        for (int32 Value : {Color.ViewRect.Min.X, Color.ViewRect.Min.Y, Color.ViewRect.Max.X, Color.ViewRect.Max.Y})
        {
            Rect.Add(MakeShared<FJsonValueNumber>(Value));
        }
        Probe.Result->SetArrayField(FString(Stage) + TEXT("_view_rect"), Rect);
    }
    return Color;
}
}
