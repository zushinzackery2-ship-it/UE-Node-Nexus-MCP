#include "NexusViewportShaderPhase.h"
#include "NexusViewportPixelProbe.h"

#include "Dom/JsonObject.h"
#include "Engine/BlendableInterface.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"
#include "SceneRendering.h"
#include "SceneManagement.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "Templates/Atomic.h"
#include "UnrealClient.h"

namespace UeNodeNexusBridge::Diagnostics
{
static FViewportShaderPhase Completed;

class FViewportViewInputs final : public ISceneViewFamilyExtentionData
{
public:
    static constexpr const TCHAR* GSubclassIdentifier = TEXT("NexusViewportViewInputs");

    virtual const TCHAR* GetSubclassIdentifier() const override
    {
        return GSubclassIdentifier;
    }

    TMap<uint32, FViewportShaderPhase> Views;
};

class FViewportPhaseExtension final : public FSceneViewExtensionBase
{
public:
    explicit FViewportPhaseExtension(const FAutoRegister& AutoRegister)
        : FSceneViewExtensionBase(AutoRegister), Target(nullptr)
    {
    }

    virtual void SetupViewFamily(FSceneViewFamily&) override
    {
    }

    virtual void SetupView(FSceneViewFamily&, FSceneView&) override
    {
    }

    virtual void BeginRenderViewFamily(FSceneViewFamily& Family) override
    {
        // Game thread only: world and post-process identity are read here and
        // passed as plain data to the render thread through the view family.
        check(IsInGameThread());
        FViewportViewInputs* Inputs = Family.GetOrCreateExtentionData<FViewportViewInputs>();
        const UWorld* World = Family.Scene ? Family.Scene->GetWorld() : nullptr;
        for (const FSceneView* View : Family.Views)
        {
            if (View->bIsSceneCapture || !View->State)
            {
                continue;
            }
            FViewportShaderPhase& State = Inputs->Views.FindOrAdd(View->GetViewKey());
            State.World = World ? World->GetPathName() : FString();
            State.WorldType = World ? static_cast<int32>(World->WorldType) : INDEX_NONE;
            State.ViewportType = World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
                ? TEXT("game") : (World && World->WorldType == EWorldType::EditorPreview ? TEXT("asset_preview") : TEXT("level"));
            State.bPostProcessing = Family.EngineShowFlags.PostProcessing;
            State.bPostProcessMaterial = Family.EngineShowFlags.PostProcessMaterial;
            State.bTonemapper = Family.EngineShowFlags.Tonemapper;
            State.ExposureBias = View->FinalPostProcessSettings.AutoExposureBias;
            State.ExposureMethod = View->FinalPostProcessSettings.AutoExposureMethod;
            FBlendableEntry* Iterator = nullptr;
            while (FPostProcessMaterialNode* Node = View->FinalPostProcessSettings.BlendableManager.IterateBlendables<FPostProcessMaterialNode>(Iterator))
            {
                const UMaterialInterface* Material = Node->GetMaterialInterface();
                const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
                if (Base)
                {
                    State.PostProcessMaterials.Add(Base->GetPathName());
                }
            }
        }
    }

    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
    {
        return Context.Viewport != nullptr && Context.Viewport == Target.Load();
    }

    virtual void PostRenderView_RenderThread(FRDGBuilder&, FSceneView& InView) override
    {
        // SceneRendering.cpp passes its actual FViewInfo here, after the main render.
        // The render thread only reads the game-thread snapshot and view uniforms.
        const FViewInfo& View = static_cast<const FViewInfo&>(InView);
        if (View.bIsSceneCapture || !View.State || !View.CachedViewUniformShaderParameters)
        {
            return;
        }
        const FViewportViewInputs* Inputs = View.Family->GetExtentionData<FViewportViewInputs>();
        const FViewportShaderPhase* Snapshot = Inputs ? Inputs->Views.Find(View.GetViewKey()) : nullptr;
        if (!Snapshot)
        {
            Completed = FViewportShaderPhase();
            return;
        }
        Completed = *Snapshot;
        const FViewUniformShaderParameters& Parameters = *View.CachedViewUniformShaderParameters;
        Completed.bAvailable = true;
        Completed.ViewKey = View.State->GetViewKey();
        Completed.EngineFrame = Parameters.FrameCounter;
        Completed.ShaderFrame = Parameters.StateFrameIndex;
        Completed.OutputFrame = Parameters.StateOutputFrameIndex;
        Completed.TemporalAA = Parameters.TemporalAAParams;
        Completed.ViewSize = FVector2f(Parameters.ViewSizeAndInvSize.X, Parameters.ViewSizeAndInvSize.Y);
        Completed.BufferSize = FVector2f(Parameters.BufferSizeAndInvSize.X, Parameters.BufferSizeAndInvSize.Y);
        Completed.PreExposure = Parameters.PreExposure;
    }

    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
        const FPostProcessingInputs& Inputs) override
    {
        ObserveViewportPixels_RenderThread(GraphBuilder, View, Inputs);
    }

    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
        FAfterPassCallbackDelegateArray& Callbacks, bool) override
    {
        if (!HasViewportPixelProbe_RenderThread(View))
        {
            return;
        }
        const TCHAR* Stage = nullptr;
        if (Pass == EPostProcessingPass::MotionBlur)
        {
            Stage = TEXT("after_temporal_aa_motion_blur");
        }
        else if (Pass == EPostProcessingPass::Tonemap)
        {
            Stage = TEXT("after_tonemap");
        }
        if (Stage)
        {
            Callbacks.Add(FAfterPassCallbackDelegate::CreateLambda(
                [Stage](FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs)
                {
                    return ObserveViewportPostProcess_RenderThread(GraphBuilder, InView, Inputs, Stage);
                }));
        }
    }

    TAtomic<FViewport*> Target;
};

static TSharedPtr<FViewportPhaseExtension, ESPMode::ThreadSafe> Extension;

void BindViewportShaderPhase(FViewport* Viewport)
{
    check(IsInGameThread());
    if (!Extension.IsValid())
    {
        Extension = FSceneViewExtensions::NewExtension<FViewportPhaseExtension>();
    }
    Extension->Target.Store(Viewport);
}

FViewportShaderPhase ReadViewportShaderPhase_RenderThread()
{
    check(IsInRenderingThread());
    return Completed;
}

bool AppendViewportRenderReceipt(const TSharedPtr<FJsonObject>& Receipt)
{
    check(IsInGameThread());
    FViewportShaderPhase State;
    ENQUEUE_RENDER_COMMAND(NexusReadCaptureProducerView)
    ([&State](FRHICommandListImmediate&)
    {
        State = ReadViewportShaderPhase_RenderThread();
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    auto View = MakeShared<FJsonObject>();
    View->SetBoolField(TEXT("available"), State.bAvailable);
    View->SetStringField(TEXT("world"), State.World);
    View->SetNumberField(TEXT("world_type"), State.WorldType);
    View->SetStringField(TEXT("viewport_type"), State.ViewportType);
    View->SetNumberField(TEXT("engine_frame"), State.EngineFrame);
    View->SetNumberField(TEXT("view_key"), State.ViewKey);
    View->SetNumberField(TEXT("pre_exposure"), State.PreExposure);
    View->SetNumberField(TEXT("exposure_method"), State.ExposureMethod);
    View->SetNumberField(TEXT("exposure_bias"), State.ExposureBias);
    View->SetBoolField(TEXT("post_processing"), State.bPostProcessing);
    View->SetBoolField(TEXT("post_process_material"), State.bPostProcessMaterial);
    View->SetBoolField(TEXT("tonemapper"), State.bTonemapper);
    TArray<TSharedPtr<FJsonValue>> Materials;
    for (const FString& Material : State.PostProcessMaterials)
    {
        Materials.Add(MakeShared<FJsonValueString>(Material));
    }
    View->SetArrayField(TEXT("post_process_materials"), Materials);
    Receipt->SetObjectField(TEXT("producer_view"), View);
    return State.bAvailable && !State.World.IsEmpty()
        && State.EngineFrame == static_cast<uint32>(Receipt->GetNumberField(TEXT("engine_frame")));
}

void StopViewportShaderPhase()
{
    check(IsInGameThread());
    if (Extension.IsValid())
    {
        Extension->Target.Store(nullptr);
    }
    Extension.Reset();
    ENQUEUE_RENDER_COMMAND(NexusStopPixelProbe)
    ([](FRHICommandListImmediate&)
    {
        Completed = FViewportShaderPhase();
        StopViewportPixelProbe_RenderThread();
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
}
}
