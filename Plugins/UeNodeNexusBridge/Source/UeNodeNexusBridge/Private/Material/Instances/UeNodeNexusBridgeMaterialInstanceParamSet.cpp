#include "UeNodeNexusBridgeMaterialInstanceParamSet.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
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

bool SetInstanceScalar(UMaterialInstanceConstant* Instance, FName Name, float Value, FString& OutError)
{
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    const FMaterialParameterInfo Info(Name);
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

bool SetInstanceVector(UMaterialInstanceConstant* Instance, FName Name, const FLinearColor& Value, FString& OutError)
{
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    const FMaterialParameterInfo Info(Name);
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

bool SetInstanceTexture(UMaterialInstanceConstant* Instance, FName Name, UTexture* Texture, FString& OutError)
{
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    const FMaterialParameterInfo Info(Name);
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

bool SetInstanceStaticSwitch(UMaterialInstanceConstant* Instance, FName Name, bool bValue, FString& OutError)
{
    if (!RequireParent(Instance, Name, OutError))
    {
        return false;
    }
    const FMaterialParameterInfo Info(Name);
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
    UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, Name, bValue);
    bool bReadBack = false;
    if (!Instance->GetStaticSwitchParameterValue(Info, bReadBack, ExpressionGuid) || bReadBack != bValue)
    {
        OutError = FString::Printf(TEXT("%s: static switch did not read back as %s"), *Name.ToString(), bValue ? TEXT("true") : TEXT("false"));
        return false;
    }
    return true;
}
}
