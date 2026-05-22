#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "FileHelpers.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
UNiagaraSystem* LoadNiagaraSystemFromPayload(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutResponse)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
    if (System == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_not_found"), TEXT("Niagara system could not be loaded")));
        return nullptr;
    }
    return System;
}

TSharedPtr<FJsonObject> MakeNiagaraAssetData(UNiagaraSystem* System)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System ? System->GetPathName() : FString());
    Data->SetStringField(TEXT("asset_class"), System ? System->GetClass()->GetPathName() : FString());
    AddNiagaraToolBoundary(Data);
    return Data;
}

static TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}

void AddNiagaraToolBoundary(TSharedPtr<FJsonObject> Data)
{
    if (!Data.IsValid())
    {
        return;
    }

    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetBoolField(TEXT("create_empty_system"), true);
    Capabilities->SetBoolField(TEXT("duplicate_existing_system"), true);
    Capabilities->SetBoolField(TEXT("read_emitters"), true);
    Capabilities->SetBoolField(TEXT("read_write_user_parameters"), true);
    Capabilities->SetBoolField(TEXT("read_write_renderer_materials"), true);
    Capabilities->SetBoolField(TEXT("compile_system"), true);
    Capabilities->SetBoolField(TEXT("read_write_system_properties"), true);
    Capabilities->SetBoolField(TEXT("author_emitters"), true);
    Capabilities->SetBoolField(TEXT("read_write_emitter_properties"), true);
    Capabilities->SetBoolField(TEXT("create_renderers"), true);
    Capabilities->SetBoolField(TEXT("read_write_renderer_properties"), true);
    Capabilities->SetBoolField(TEXT("author_emitter_stack_modules"), true);
    Capabilities->SetBoolField(TEXT("read_write_module_inputs"), false);
    Data->SetObjectField(TEXT("capabilities"), Capabilities);

    Data->SetArrayField(TEXT("limitations"), {
        MakeStringValue(TEXT("module_stack_input_authoring_is_not_exposed_as_a_public_tool")),
        MakeStringValue(TEXT("add_existing_module_scripts_then_edit_system_emitter_renderer_properties_user_params_renderer_materials"))
    });
    Data->SetStringField(TEXT("recommended_generic_workflow"), TEXT("create_or_duplicate_system; add default/minimal emitters; add existing module scripts; add sprite/ribbon/mesh/light renderers; edit properties/user params/materials; compile and save"));
}

bool NiagaraSystemHasEmitterStack(UNiagaraSystem* System)
{
    return System != nullptr && System->GetEmitterHandles().Num() > 0;
}

int32 CountNiagaraRenderers(UNiagaraSystem* System)
{
    int32 RendererCount = 0;
    if (System == nullptr)
    {
        return RendererCount;
    }
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
        RendererCount += EmitterData ? EmitterData->GetRenderers().Num() : 0;
    }
    return RendererCount;
}

TSharedPtr<FJsonObject> MakeNiagaraBoundaryWarning(UNiagaraSystem* System, const FString& Code, const FString& Message)
{
    return MakeDiagnostic(TEXT("warning"), Code, Message, System ? System->GetPathName() : FString(), TEXT("UeNodeNexusBridge.Niagara"));
}

void AppendNiagaraEmptySystemWarning(UNiagaraSystem* System, TArray<TSharedPtr<FJsonValue>>& Warnings)
{
    if (NiagaraSystemHasEmitterStack(System))
    {
        return;
    }
    Warnings.Add(MakeShared<FJsonValueObject>(MakeNiagaraBoundaryWarning(
        System,
        TEXT("niagara_empty_system_not_runtime_vfx"),
        TEXT("Niagara system has no emitters. Add an emitter before treating the system as a runtime VFX."))));
}

TSharedPtr<FJsonObject> MakeNiagaraEmitterJson(UNiagaraSystem* System, int32 EmitterIndex)
{
    const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
    const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
    const int32 RendererCount = EmitterData ? EmitterData->GetRenderers().Num() : 0;

    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("index"), EmitterIndex);
    Json->SetStringField(TEXT("name"), Handle.GetName().ToString());
    Json->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
    Json->SetNumberField(TEXT("renderer_count"), RendererCount);
    return Json;
}

TSharedPtr<FJsonValue> MakeNiagaraEmitterRow(UNiagaraSystem* System, int32 EmitterIndex)
{
    const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
    const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
    const int32 RendererCount = EmitterData ? EmitterData->GetRenderers().Num() : 0;

    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueNumber>(EmitterIndex));
    Row.Add(MakeShared<FJsonValueString>(Handle.GetName().ToString()));
    Row.Add(MakeShared<FJsonValueBoolean>(Handle.GetIsEnabled()));
    Row.Add(MakeShared<FJsonValueNumber>(RendererCount));
    return MakeShared<FJsonValueArray>(Row);
}

FString GetRendererMaterialPath(UNiagaraRendererProperties* Renderer, int32 MaterialIndex)
{
    if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
    {
        return Sprite->Material ? Sprite->Material->GetPathName() : FString();
    }
    if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
    {
        return Ribbon->Material ? Ribbon->Material->GetPathName() : FString();
    }
    if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
    {
        if (Mesh->OverrideMaterials.IsValidIndex(MaterialIndex))
        {
            UMaterialInterface* Material = Mesh->OverrideMaterials[MaterialIndex].ExplicitMat;
            return Material ? Material->GetPathName() : FString();
        }
    }
    return FString();
}

bool SetRendererMaterial(UNiagaraRendererProperties* Renderer, UMaterialInterface* Material, int32 MaterialIndex)
{
    if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
    {
        Sprite->Modify();
        Sprite->Material = Material;
        return true;
    }
    if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
    {
        Ribbon->Modify();
        Ribbon->Material = Material;
        return true;
    }
    if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(Renderer))
    {
        Mesh->Modify();
        Mesh->bOverrideMaterials = true;
        const int32 TargetIndex = FMath::Max(0, MaterialIndex);
        while (Mesh->OverrideMaterials.Num() <= TargetIndex)
        {
            Mesh->OverrideMaterials.AddDefaulted();
        }
        Mesh->OverrideMaterials[TargetIndex].ExplicitMat = Material;
        return true;
    }
    return false;
}

TSharedPtr<FJsonObject> MakeNiagaraMaterialJson(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer, int32 MaterialIndex)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("emitter_index"), EmitterIndex);
    Json->SetNumberField(TEXT("renderer_index"), RendererIndex);
    Json->SetNumberField(TEXT("material_index"), MaterialIndex);
    Json->SetStringField(TEXT("renderer_class"), Renderer ? Renderer->GetClass()->GetName() : FString());
    Json->SetStringField(TEXT("material_path"), GetRendererMaterialPath(Renderer, MaterialIndex));
    return Json;
}

TSharedPtr<FJsonValue> MakeNiagaraMaterialRow(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer, int32 MaterialIndex)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueNumber>(EmitterIndex));
    Row.Add(MakeShared<FJsonValueNumber>(RendererIndex));
    Row.Add(MakeShared<FJsonValueNumber>(MaterialIndex));
    Row.Add(MakeShared<FJsonValueString>(Renderer ? Renderer->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(GetRendererMaterialPath(Renderer, MaterialIndex)));
    return MakeShared<FJsonValueArray>(Row);
}

bool SaveAssetPackage(UObject* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    return Package != nullptr && UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
}
}
