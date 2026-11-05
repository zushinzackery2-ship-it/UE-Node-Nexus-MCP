#include "UeNodeNexusBridgeMaterialInstanceParamSet.h"

#include "Engine/Texture.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"

namespace UeNodeNexusBridge
{
namespace
{
bool RequireParent(UMaterialInstanceConstant* Instance, FName Name, FString& OutError)
{
    if (Instance == nullptr)
    {
        OutError = TEXT("instance is null");
        return false;
    }
    if (Instance->Parent == nullptr)
    {
        OutError = FString::Printf(TEXT("%s: instance has no parent material"), *Name.ToString());
        return false;
    }
    return true;
}

FString NotExposed(FName Name, const TCHAR* Kind)
{
    return FString::Printf(TEXT("%s: parent material exposes no %s parameter with that name"), *Name.ToString(), Kind);
}
}

bool SetInstanceScalar(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, float Value, FString& OutError)
{
    const FName Name = Info.Name;
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    float ParentDefault = 0.0f;
    if (!Instance->Parent->GetScalarParameterDefaultValue(Info, ParentDefault))
    {
        OutError = NotExposed(Name, TEXT("scalar"));
        return false;
    }
    Instance->SetScalarParameterValueEditorOnly(Info, Value);
    float ReadBack = 0.0f;
    if (!Instance->GetScalarParameterValue(Info, ReadBack) || !FMath::IsNearlyEqual(ReadBack, Value, 1e-5f))
    {
        OutError = FString::Printf(TEXT("%s: wrote %g but read back %g"), *Name.ToString(), Value, ReadBack);
        return false;
    }
    return true;
}

bool SetInstanceVector(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, const FLinearColor& Value, FString& OutError)
{
    const FName Name = Info.Name;
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    FLinearColor ParentDefault = FLinearColor::Black;
    if (!Instance->Parent->GetVectorParameterDefaultValue(Info, ParentDefault))
    {
        OutError = NotExposed(Name, TEXT("vector"));
        return false;
    }
    Instance->SetVectorParameterValueEditorOnly(Info, Value);
    FLinearColor ReadBack = FLinearColor::Black;
    if (!Instance->GetVectorParameterValue(Info, ReadBack) || !ReadBack.Equals(Value, 1e-5f))
    {
        OutError = FString::Printf(TEXT("%s: wrote %s but read back %s"), *Name.ToString(), *Value.ToString(), *ReadBack.ToString());
        return false;
    }
    return true;
}

bool SetInstanceTexture(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, UTexture* Texture, FString& OutError)
{
    const FName Name = Info.Name;
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    UTexture* ParentDefault = nullptr;
    if (!Instance->Parent->GetTextureParameterDefaultValue(Info, ParentDefault))
    {
        OutError = NotExposed(Name, TEXT("texture"));
        return false;
    }
    Instance->SetTextureParameterValueEditorOnly(Info, Texture);
    UTexture* ReadBack = nullptr;
    if (!Instance->GetTextureParameterValue(Info, ReadBack) || ReadBack != Texture)
    {
        OutError = FString::Printf(TEXT("%s: texture did not read back as %s"), *Name.ToString(), Texture ? *Texture->GetPathName() : TEXT("None"));
        return false;
    }
    return true;
}

bool SetInstanceStaticSwitch(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info, bool bValue, FString& OutError)
{
    const FName Name = Info.Name;
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    bool bParentDefault = false;
    FGuid ExpressionGuid;
    if (!Instance->Parent->GetStaticSwitchParameterDefaultValue(Info, bParentDefault, ExpressionGuid))
    {
        OutError = NotExposed(Name, TEXT("static switch"));
        return false;
    }
    // The library variant also rebuilds MaterialLayersParameters so the instance
    // editor does not wipe the switch later; keep that side effect, ignore the
    // dead return value, and verify ourselves.
    Instance->SetStaticSwitchParameterValueEditorOnly(Info, bValue);
    auto* EditorInstance = NewObject<UMaterialEditorInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transactional);
    EditorInstance->SetSourceInstance(Instance);
    bool bReadBack = false;
    if (!Instance->GetStaticSwitchParameterValue(Info, bReadBack, ExpressionGuid) || bReadBack != bValue)
    {
        OutError = FString::Printf(TEXT("%s: static switch did not read back as %s"), *Name.ToString(), bValue ? TEXT("true") : TEXT("false"));
        return false;
    }
    return true;
}

bool SetInstanceComponentMask(UMaterialInstanceConstant* Instance, const FMaterialParameterInfo& Info,
    bool R, bool G, bool B, bool A, FString& OutError)
{
    if (!RequireParent(Instance, Info.Name, OutError))
    {
        return false;
    }
    bool ParentR = false, ParentG = false, ParentB = false, ParentA = false;
    FGuid Guid;
    if (!Instance->Parent->GetStaticComponentMaskParameterDefaultValue(Info, ParentR, ParentG, ParentB, ParentA, Guid))
    {
        OutError = NotExposed(Info.Name, TEXT("static component mask"));
        return false;
    }
    FStaticParameterSet Parameters = Instance->GetStaticParameters();
    auto* Existing = Parameters.EditorOnly.StaticComponentMaskParameters.FindByPredicate(
        [&Info](const FStaticComponentMaskParameter& Item) { return Item.ParameterInfo == Info; });
    if (Existing == nullptr)
    {
        Existing = &Parameters.EditorOnly.StaticComponentMaskParameters.AddDefaulted_GetRef();
        Existing->ParameterInfo = Info;
    }
    Existing->ExpressionGUID = Guid;
    Existing->bOverride = true;
    Existing->R = R;
    Existing->G = G;
    Existing->B = B;
    Existing->A = A;
    Instance->UpdateStaticPermutation(Parameters);
    // UE 5.5's mask convenience getter queries StaticSwitch by mistake.
    // Use the typed parameter API so verification reads the mask we wrote.
    FMaterialParameterMetadata Actual;
    if (!Instance->GetParameterValue(EMaterialParameterType::StaticComponentMask, Info, Actual)
        || Actual.Value.Bool[0] != R || Actual.Value.Bool[1] != G || Actual.Value.Bool[2] != B || Actual.Value.Bool[3] != A)
    {
        OutError = FString::Printf(TEXT("%s: component mask did not read back its assigned channels"), *Info.Name.ToString());
        return false;
    }
    return true;
}
}
