#include "Lint/UeNodeNexusBridgeNiagaraAssetLintRules.h"

#include "Lint/UeNodeNexusBridgeNiagaraAssetLintShared.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Modules/UeNodeNexusBridgeNiagaraModuleStack.h"

#include "Dom/JsonValue.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge::NiagaraAssetLint
{
namespace
{
bool IsMaterialOutputConnected(UMaterial* Material, EMaterialProperty Property)
{
    if (Material == nullptr)
    {
        return false;
    }
    FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
    return Input != nullptr && Input->Expression != nullptr;
}

void LintMaterial(UMaterialInterface* Interface, int32 EmitterIndex, int32 RendererIndex, TArray<TSharedPtr<FJsonValue>>& Issues)
{
    UMaterial* Material = Interface ? Interface->GetMaterial() : nullptr;
    if (Material == nullptr)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("renderer_material_missing"), TEXT("Renderer has no resolvable material."), EmitterIndex, RendererIndex));
        return;
    }

    if (Material->BlendMode == BLEND_Opaque)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("warning"), TEXT("sprite_material_opaque"), TEXT("Sprite renderer material is opaque; particle cards may render as hard rectangles."), EmitterIndex, RendererIndex));
    }
    if (Material->GetShadingModels().HasShadingModel(MSM_DefaultLit))
    {
        AddIssue(Issues, MakeLintIssue(TEXT("info"), TEXT("vfx_material_default_lit"), TEXT("VFX material uses DefaultLit shading; unlit is often more predictable for emissive particles."), EmitterIndex, RendererIndex));
    }
    if (!IsMaterialOutputConnected(Material, MP_EmissiveColor) && !IsMaterialOutputConnected(Material, MP_BaseColor))
    {
        AddIssue(Issues, MakeLintIssue(TEXT("warning"), TEXT("material_no_color_output"), TEXT("Material has no EmissiveColor or BaseColor expression connected."), EmitterIndex, RendererIndex));
    }
    if (Material->BlendMode != BLEND_Opaque && !IsMaterialOutputConnected(Material, MP_Opacity) && !IsMaterialOutputConnected(Material, MP_OpacityMask))
    {
        AddIssue(Issues, MakeLintIssue(TEXT("warning"), TEXT("translucent_material_no_opacity"), TEXT("Non-opaque material has no Opacity or OpacityMask expression connected."), EmitterIndex, RendererIndex));
    }
}

void LintRenderer(UNiagaraRendererProperties* Renderer, int32 EmitterIndex, int32 RendererIndex, TArray<TSharedPtr<FJsonValue>>& Issues)
{
    if (Renderer == nullptr)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("renderer_null"), TEXT("Renderer entry is null."), EmitterIndex, RendererIndex));
        return;
    }
    if (!Renderer->GetIsEnabled())
    {
        return;
    }
    if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
    {
        if (Sprite->bCastShadows)
        {
            AddIssue(Issues, MakeLintIssue(TEXT("info"), TEXT("sprite_renderer_casts_shadows"), TEXT("Sprite renderer casts shadows; this can make soft VFX read as dark cards."), EmitterIndex, RendererIndex));
        }
        LintMaterial(Sprite->Material, EmitterIndex, RendererIndex, Issues);
    }
}

void LintModules(FNiagaraEmitterHandle& Handle, int32 EmitterIndex, TArray<TSharedPtr<FJsonValue>>& Issues)
{
    static const TArray<FString> Usages = {
        TEXT("EmitterUpdateScript"),
        TEXT("ParticleSpawnScript"),
        TEXT("ParticleUpdateScript")
    };

    for (const FString& Usage : Usages)
    {
        UNiagaraNodeOutput* Output = NiagaraModuleStack::ResolveOutputNode(&Handle, Usage);
        TArray<UNiagaraNodeFunctionCall*> Modules;
        NiagaraModuleStack::GetOrderedModules(Output, Modules);
        for (int32 Index = 0; Index < Modules.Num(); ++Index)
        {
            UNiagaraNodeFunctionCall* Module = Modules[Index];
            if (Module == nullptr || !Module->IsNodeEnabled())
            {
                continue;
            }
            const FString FunctionName = Module->GetFunctionName();
            if (Usage == TEXT("EmitterUpdateScript") && FunctionName.Contains(TEXT("SpawnRate")))
            {
                AddIssue(Issues, MakeLintIssue(TEXT("info"), TEXT("spawn_rate_present"), TEXT("Emitter uses SpawnRate; verify it is intentionally non-zero."), EmitterIndex, INDEX_NONE, Usage, Index));
            }
            if (Usage == TEXT("ParticleSpawnScript") && FunctionName.Contains(TEXT("AddVelocity")))
            {
                AddIssue(Issues, MakeLintIssue(TEXT("info"), TEXT("spawn_velocity_present"), TEXT("Spawn-time AddVelocity is enabled; high values can make effects read as jets."), EmitterIndex, INDEX_NONE, Usage, Index));
            }
        }
    }
}

void LintEmitter(UNiagaraSystem* System, int32 EmitterIndex, TArray<TSharedPtr<FJsonValue>>& Issues)
{
    FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
    if (!Handle.GetIsEnabled())
    {
        return;
    }

    FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
    if (EmitterData == nullptr)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("emitter_data_missing"), TEXT("Enabled emitter has no emitter data."), EmitterIndex));
        return;
    }

    const auto& Renderers = EmitterData->GetRenderers();
    if (Renderers.Num() == 0)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("emitter_has_no_renderers"), TEXT("Enabled emitter has no renderer."), EmitterIndex));
    }
    for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
    {
        LintRenderer(Renderers[RendererIndex], EmitterIndex, RendererIndex, Issues);
    }
    LintModules(Handle, EmitterIndex, Issues);
}
}

void CollectSystemIssues(UNiagaraSystem* System, TArray<TSharedPtr<FJsonValue>>& Issues)
{
    if (!System->IsReadyToRun())
    {
        AddIssue(Issues, MakeLintIssue(TEXT("warning"), TEXT("system_not_ready_to_run"), TEXT("Niagara system reports IsReadyToRun=false.")));
    }
    if (System->GetEmitterHandles().Num() == 0)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("system_has_no_emitters"), TEXT("Niagara system has no emitters.")));
    }
    if (CountEnabledNiagaraEmitters(System) == 0)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("system_has_no_enabled_emitters"), TEXT("Niagara system has no enabled emitters.")));
    }
    if (CountEnabledNiagaraRenderers(System) == 0)
    {
        AddIssue(Issues, MakeLintIssue(TEXT("error"), TEXT("system_has_no_enabled_renderers"), TEXT("Niagara system has no enabled renderers.")));
    }

    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        LintEmitter(System, EmitterIndex, Issues);
    }
}
}
