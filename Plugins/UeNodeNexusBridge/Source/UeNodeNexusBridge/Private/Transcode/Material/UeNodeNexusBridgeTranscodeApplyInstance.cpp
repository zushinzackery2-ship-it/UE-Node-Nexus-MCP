#include "UeNodeNexusBridgeTranscode.h"
#include "Instance/NexusInstanceValues.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Material/Instances/UeNodeNexusBridgeMaterialInstanceParamSet.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
using UeNodeNexusBridge::SetInstanceScalar;
using UeNodeNexusBridge::SetInstanceTexture;
using UeNodeNexusBridge::SetInstanceVector;

namespace
{
FString ReadString(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field)
{
    FString Value;
    Op->TryGetStringField(Field, Value);
    return Value;
}

bool SetParam(UMaterialInstanceConstant* Instance, const FString& Kind, const FMaterialParameterInfo& Info, const FString& Value, FString& OutError)
{
    const FString Name = Info.Name.ToString();
    if (Kind == TEXT("scalar"))
    {
        float Number = 0;
        if (!InstanceFloat(Value, Number))
        {
            OutError = Name + TEXT(": expected a complete finite float, got ") + Value;
            return false;
        }
        return SetInstanceScalar(Instance, Info, Number, OutError);
    }
    if (Kind == TEXT("vector"))
    {
        FLinearColor Color;
        TArray<FString> Channels;
        if (!InstanceChannels(Value, Channels) || !InstanceFloat(Channels[0], Color.R)
            || !InstanceFloat(Channels[1], Color.G) || !InstanceFloat(Channels[2], Color.B) || !InstanceFloat(Channels[3], Color.A))
        {
            OutError = FString::Printf(TEXT("%s: expected (R=..,G=..,B=..,A=..), got %s"), *Name, *Value);
            return false;
        }
        return SetInstanceVector(Instance, Info, Color, OutError);
    }
    if (Kind == TEXT("texture"))
    {
        UTexture* Texture = Cast<UTexture>(ResolveObjectByPath(Value));
        if (Texture == nullptr)
        {
            OutError = FString::Printf(TEXT("%s: texture not found: %s"), *Name, *Value);
            return false;
        }
        return SetInstanceTexture(Instance, Info, Texture, OutError);
    }
    if (Kind == TEXT("switch"))
    {
        bool bValue = false;
        if (!InstanceBool(Value, bValue))
        {
            OutError = FString::Printf(TEXT("%s: expected true/false, got %s"), *Name, *Value);
            return false;
        }
        return SetInstanceStaticSwitch(Instance, Info, bValue, OutError);
    }
    if (Kind == TEXT("component_mask"))
    {
        TArray<FString> Channels;
        bool R = false, G = false, B = false, A = false;
        if (!InstanceChannels(Value, Channels) || !InstanceBool(Channels[0], R) || !InstanceBool(Channels[1], G)
            || !InstanceBool(Channels[2], B) || !InstanceBool(Channels[3], A))
        {
            OutError = Name + TEXT(": expected four distinct RGBA boolean channels, got ") + Value;
            return false;
        }
        return SetInstanceComponentMask(Instance, Info, R, G, B, A, OutError);
    }
    OutError = FString::Printf(TEXT("unsupported parameter kind: %s"), *Kind);
    return false;
}

template <typename TValue>
void RemoveParameter(TArray<TValue>& Values, const FMaterialParameterInfo& Info)
{
    Values.RemoveAll([&Info](const TValue& Item) { return Item.ParameterInfo == Info; });
}

bool ClearParam(UMaterialInstanceConstant* Instance, const FString& Kind, const FMaterialParameterInfo& Info, FString& OutError)
{
    Instance->Modify();
    if (Kind == TEXT("scalar"))
    {
        RemoveParameter(Instance->ScalarParameterValues, Info);
    }
    else if (Kind == TEXT("vector"))
    {
        RemoveParameter(Instance->VectorParameterValues, Info);
    }
    else if (Kind == TEXT("texture"))
    {
        RemoveParameter(Instance->TextureParameterValues, Info);
    }
    else if (Kind == TEXT("runtime_virtual_texture"))
    {
        RemoveParameter(Instance->RuntimeVirtualTextureParameterValues, Info);
    }
    else if (Kind == TEXT("switch"))
    {
        FStaticParameterSet Parameters = Instance->GetStaticParameters();
        RemoveParameter(Parameters.StaticSwitchParameters, Info);
        Instance->UpdateStaticPermutation(Parameters);
    }
    else if (Kind == TEXT("component_mask"))
    {
        FStaticParameterSet Parameters = Instance->GetStaticParameters();
        RemoveParameter(Parameters.EditorOnly.StaticComponentMaskParameters, Info);
        Instance->UpdateStaticPermutation(Parameters);
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
        FMaterialParameterInfo Info;
        if ((Verb == TEXT("mi_set_param") || Verb == TEXT("mi_clear_param")) && !InstanceParameterInfo(Op, Info, Error))
        {
            Context.Fail(Index, TEXT("invalid_parameter_identity"), Error);
            continue;
        }
        bool bOk = false;
        if (Verb == TEXT("set_asset_prop"))
        {
            bOk = ImportPropertyValue(Instance, ReadString(Op, TEXT("name")), ReadString(Op, TEXT("value")), Error);
        }
        else if (Verb == TEXT("mi_set_param"))
        {
            bOk = SetParam(Instance, ReadString(Op, TEXT("kind")), Info, ReadString(Op, TEXT("value")), Error);
            if (!bOk && Error.IsEmpty())
            {
                Error = FString::Printf(TEXT("parameter %s could not be set (not exposed by the parent material?)"), *ReadString(Op, TEXT("name")));
            }
        }
        else if (Verb == TEXT("mi_clear_param"))
        {
            bOk = ClearParam(Instance, ReadString(Op, TEXT("kind")), Info, Error);
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
