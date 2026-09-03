#include "UeNodeNexusBridgeTranscode.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
FString ReadString(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field)
{
    FString Value;
    Op->TryGetStringField(Field, Value);
    return Value;
}

bool ParseBool(const FString& Text, bool& bOut)
{
    const FString Lower = Text.TrimStartAndEnd().ToLower();
    if (Lower == TEXT("true") || Lower == TEXT("1"))
    {
        bOut = true;
        return true;
    }
    if (Lower == TEXT("false") || Lower == TEXT("0"))
    {
        bOut = false;
        return true;
    }
    return false;
}

bool SetStaticSwitch(UMaterialInstanceConstant* Instance, const FName Name, bool bOverride, bool bValue)
{
    FStaticParameterSet Parameters = Instance->GetStaticParameters();
    FStaticSwitchParameter* Existing = Parameters.StaticSwitchParameters.FindByPredicate([Name](const FStaticSwitchParameter& Item) { return Item.ParameterInfo.Name == Name; });
    if (Existing == nullptr)
    {
        if (!bOverride)
        {
            return true;
        }
        Existing = &Parameters.StaticSwitchParameters.AddDefaulted_GetRef();
        Existing->ParameterInfo = FMaterialParameterInfo(Name);
    }
    Existing->bOverride = bOverride;
    Existing->Value = bValue;
    Instance->UpdateStaticPermutation(Parameters);
    return true;
}

bool SetComponentMask(UMaterialInstanceConstant* Instance, const FName Name, bool bOverride, const FString& Text)
{
    FStaticParameterSet Parameters = Instance->GetStaticParameters();
    FStaticComponentMaskParameter* Existing = Parameters.EditorOnly.StaticComponentMaskParameters.FindByPredicate([Name](const FStaticComponentMaskParameter& Item) { return Item.ParameterInfo.Name == Name; });
    if (Existing == nullptr)
    {
        if (!bOverride)
        {
            return true;
        }
        Existing = &Parameters.EditorOnly.StaticComponentMaskParameters.AddDefaulted_GetRef();
        Existing->ParameterInfo = FMaterialParameterInfo(Name);
    }
    Existing->bOverride = bOverride;
    if (bOverride)
    {
        auto Channel = [&Text](const TCHAR* Key)
        {
            FString Value;
            return FParse::Value(*Text, Key, Value) && Value.StartsWith(TEXT("T"), ESearchCase::IgnoreCase);
        };
        Existing->R = Channel(TEXT("R="));
        Existing->G = Channel(TEXT("G="));
        Existing->B = Channel(TEXT("B="));
        Existing->A = Channel(TEXT("A="));
    }
    Instance->UpdateStaticPermutation(Parameters);
    return true;
}

bool SetParam(UMaterialInstanceConstant* Instance, const FString& Kind, const FString& Name, const FString& Value, FString& OutError)
{
    const FName ParameterName(*Name);
    if (Kind == TEXT("scalar"))
    {
        return UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, ParameterName, FCString::Atof(*Value));
    }
    if (Kind == TEXT("vector"))
    {
        FLinearColor Color;
        if (!Color.InitFromString(Value))
        {
            OutError = FString::Printf(TEXT("%s: expected (R=..,G=..,B=..,A=..), got %s"), *Name, *Value);
            return false;
        }
        return UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, ParameterName, Color);
    }
    if (Kind == TEXT("texture"))
    {
        UTexture* Texture = Cast<UTexture>(ResolveObjectByPath(Value));
        if (Texture == nullptr)
        {
            OutError = FString::Printf(TEXT("%s: texture not found: %s"), *Name, *Value);
            return false;
        }
        return UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, ParameterName, Texture);
    }
    if (Kind == TEXT("switch"))
    {
        bool bValue = false;
        if (!ParseBool(Value, bValue))
        {
            OutError = FString::Printf(TEXT("%s: expected true/false, got %s"), *Name, *Value);
            return false;
        }
        return SetStaticSwitch(Instance, ParameterName, true, bValue);
    }
    if (Kind == TEXT("component_mask"))
    {
        return SetComponentMask(Instance, ParameterName, true, Value);
    }
    OutError = FString::Printf(TEXT("unsupported parameter kind: %s"), *Kind);
    return false;
}

template <typename TValue>
void RemoveNamed(TArray<TValue>& Values, const FName Name)
{
    Values.RemoveAll([Name](const TValue& Item) { return Item.ParameterInfo.Name == Name; });
}

bool ClearParam(UMaterialInstanceConstant* Instance, const FString& Kind, const FString& Name, FString& OutError)
{
    const FName ParameterName(*Name);
    Instance->Modify();
    if (Kind == TEXT("scalar"))
    {
        RemoveNamed(Instance->ScalarParameterValues, ParameterName);
    }
    else if (Kind == TEXT("vector"))
    {
        RemoveNamed(Instance->VectorParameterValues, ParameterName);
    }
    else if (Kind == TEXT("texture"))
    {
        RemoveNamed(Instance->TextureParameterValues, ParameterName);
    }
    else if (Kind == TEXT("runtime_virtual_texture"))
    {
        RemoveNamed(Instance->RuntimeVirtualTextureParameterValues, ParameterName);
    }
    else if (Kind == TEXT("switch"))
    {
        return SetStaticSwitch(Instance, ParameterName, false, false);
    }
    else if (Kind == TEXT("component_mask"))
    {
        return SetComponentMask(Instance, ParameterName, false, FString());
    }
    else
    {
        OutError = FString::Printf(TEXT("unsupported parameter kind: %s"), *Kind);
        return false;
    }
    return true;
}
}

void ApplyMaterialInstancePlan(UMaterialInstanceConstant* Instance, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    if (Instance == nullptr)
    {
        Context.Fail(INDEX_NONE, TEXT("invalid_asset"), TEXT("asset is not a MaterialInstanceConstant"));
        return;
    }
    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Op = Plan[Index].IsValid() ? Plan[Index]->AsObject() : nullptr;
        if (!Op.IsValid())
        {
            Context.Fail(Index, TEXT("invalid_verb"), TEXT("plan entries must be objects"));
            continue;
        }
        const FString Verb = ReadString(Op, TEXT("op"));
        if (Context.bDryRun)
        {
            continue;
        }
        FString Error;
        bool bOk = false;
        if (Verb == TEXT("set_asset_prop"))
        {
            bOk = ImportPropertyValue(Instance, ReadString(Op, TEXT("name")), ReadString(Op, TEXT("value")), Error);
        }
        else if (Verb == TEXT("mi_set_param"))
        {
            bOk = SetParam(Instance, ReadString(Op, TEXT("kind")), ReadString(Op, TEXT("name")), ReadString(Op, TEXT("value")), Error);
            if (!bOk && Error.IsEmpty())
            {
                Error = FString::Printf(TEXT("parameter %s could not be set (not exposed by the parent material?)"), *ReadString(Op, TEXT("name")));
            }
        }
        else if (Verb == TEXT("mi_clear_param"))
        {
            bOk = ClearParam(Instance, ReadString(Op, TEXT("kind")), ReadString(Op, TEXT("name")), Error);
        }
        else
        {
            Error = FString::Printf(TEXT("%s is not supported on material instances"), *Verb);
        }
        if (!bOk)
        {
            Context.Fail(Index, TEXT("apply_failed"), Error);
            continue;
        }
        Context.bChanged = true;
    }
    if (Context.bChanged && !Context.bDryRun)
    {
        Instance->PostEditChange();
    }
}
}
