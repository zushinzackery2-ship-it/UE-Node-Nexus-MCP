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
bool ReadNiagaraIndexField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue)
{
    double Number = -1.0;
    if (!Payload->TryGetNumberField(Field, Number))
    {
        OutValue = INDEX_NONE;
        return false;
    }
    OutValue = static_cast<int32>(Number);
    return true;
}

FNiagaraEmitterHandle* ResolveNiagaraEmitterHandle(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    int32 EmitterIndex = INDEX_NONE;
    if (!ReadNiagaraIndexField(Payload, TEXT("emitter_index"), EmitterIndex) || !System->GetEmitterHandles().IsValidIndex(EmitterIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_emitter_index"), TEXT("emitter_index is required and must point to an existing emitter")));
        return nullptr;
    }
    return &System->GetEmitterHandles()[EmitterIndex];
}

TSharedPtr<FJsonObject> MakeNiagaraAssetData(UNiagaraSystem* System)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System ? System->GetPathName() : FString());
    Data->SetStringField(TEXT("asset_class"), System ? System->GetClass()->GetPathName() : FString());
    return Data;
}

TSharedPtr<FJsonObject> MakeNiagaraAssetSummaryData(UNiagaraSystem* System)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), System ? System->GetPathName() : FString());
    return Data;
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

int32 CountEnabledNiagaraEmitters(UNiagaraSystem* System)
{
    int32 EmitterCount = 0;
    if (System == nullptr)
    {
        return EmitterCount;
    }
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetIsEnabled())
        {
            ++EmitterCount;
        }
    }
    return EmitterCount;
}

int32 CountEnabledNiagaraRenderers(UNiagaraSystem* System)
{
    int32 RendererCount = 0;
    if (System == nullptr)
    {
        return RendererCount;
    }
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (!Handle.GetIsEnabled())
        {
            continue;
        }
        const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
        {
            if (Renderer != nullptr && Renderer->GetIsEnabled())
            {
                ++RendererCount;
            }
        }
    }
    return RendererCount;
}

int32 CountNiagaraReadinessIssues(UNiagaraSystem* System)
{
    int32 IssueCount = 0;
    if (System == nullptr || !System->IsReadyToRun())
    {
        ++IssueCount;
    }
    if (!NiagaraSystemHasEmitterStack(System))
    {
        ++IssueCount;
    }
    if (CountEnabledNiagaraEmitters(System) == 0)
    {
        ++IssueCount;
    }
    if (CountEnabledNiagaraRenderers(System) == 0)
    {
        ++IssueCount;
    }
    return IssueCount;
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
        TEXT("niagara_empty_system_no_emitters"),
        TEXT("Niagara system has no emitters. Add an emitter before treating the asset as ready."))));
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
