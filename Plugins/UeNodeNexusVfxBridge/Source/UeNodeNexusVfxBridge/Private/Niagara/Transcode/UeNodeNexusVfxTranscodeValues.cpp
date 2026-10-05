#include "UeNodeNexusVfxTranscode.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraDataInterface.h"
#include "NiagaraTypes.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "Values/NexusNiagaraValue.h"
#include "Values/Storage/NexusNiagaraStorage.h"

namespace UeNodeNexusBridge::VfxTranscode
{
static const TPair<const TCHAR*, ENiagaraScriptUsage> GGroups[] = {
    { TEXT("EmitterSpawn"), ENiagaraScriptUsage::EmitterSpawnScript },
    { TEXT("EmitterUpdate"), ENiagaraScriptUsage::EmitterUpdateScript },
    { TEXT("ParticleSpawn"), ENiagaraScriptUsage::ParticleSpawnScript },
    { TEXT("ParticleUpdate"), ENiagaraScriptUsage::ParticleUpdateScript },
};

bool UsageFromGroup(const FString& Group, ENiagaraScriptUsage& OutUsage)
{
    for (const TPair<const TCHAR*, ENiagaraScriptUsage>& Pair : GGroups)
    {
        if (Group.Equals(Pair.Key, ESearchCase::IgnoreCase) || Group.Equals(FString(Pair.Key) + TEXT("Script"), ESearchCase::IgnoreCase))
        {
            OutUsage = Pair.Value;
            return true;
        }
    }
    return false;
}

FString GroupFromUsage(ENiagaraScriptUsage Usage)
{
    for (const TPair<const TCHAR*, ENiagaraScriptUsage>& Pair : GGroups)
    {
        if (Pair.Value == Usage)
        {
            return Pair.Key;
        }
    }
    const UEnum* Enum = StaticEnum<ENiagaraScriptUsage>();
    return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Usage)) : FString();
}

// Text names for the common Niagara value types (struct names like "NiagaraFloat" are accepted on input too).
struct FTypeAlias
{
    const TCHAR* Name;
    const FNiagaraTypeDefinition& (*Get)();
};
static const FTypeAlias GTypeAliases[] = {
    { TEXT("float"), &FNiagaraTypeDefinition::GetFloatDef },
    { TEXT("int"), &FNiagaraTypeDefinition::GetIntDef },
    { TEXT("bool"), &FNiagaraTypeDefinition::GetBoolDef },
    { TEXT("Vector2"), &FNiagaraTypeDefinition::GetVec2Def },
    { TEXT("Vector"), &FNiagaraTypeDefinition::GetVec3Def },
    { TEXT("Vector4"), &FNiagaraTypeDefinition::GetVec4Def },
    { TEXT("Color"), &FNiagaraTypeDefinition::GetColorDef },
    { TEXT("Position"), &FNiagaraTypeDefinition::GetPositionDef },
    { TEXT("Quat"), &FNiagaraTypeDefinition::GetQuatDef },
};

FString FriendlyTypeName(const FNiagaraTypeDefinition& Type)
{
    for (const FTypeAlias& Alias : GTypeAliases)
    {
        if (Alias.Get() == Type)
        {
            return Alias.Name;
        }
    }
    if (Type.GetClass() != nullptr)
    {
        return Type.GetClass()->GetName();
    }
    return Type.GetName();
}

FNiagaraTypeDefinition TypeFromName(const FString& Name)
{
    for (const FTypeAlias& Alias : GTypeAliases)
    {
        if (Name.Equals(Alias.Name, ESearchCase::IgnoreCase) || Name.Equals(Alias.Get().GetName(), ESearchCase::IgnoreCase))
        {
            return Alias.Get();
        }
    }
    if (Name.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetIntDef();
    }
    if (Name.Equals(TEXT("vec2"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec2Def();
    }
    if (Name.Equals(TEXT("vec3"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec3Def();
    }
    if (Name.Equals(TEXT("vec4"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec4Def();
    }
    if (Name.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetColorDef();
    }
    if (Name.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition(UMaterialInterface::StaticClass());
    }
    if (UClass* Class = UClass::TryFindTypeSlow<UClass>(Name))
    {
        return FNiagaraTypeDefinition(Class);
    }
    return FNiagaraTypeDefinition();
}

bool SetParameterValueText(FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, const FString& Text, bool bAddIfMissing, FString* OutError)
{
    if (OutError)
    {
        OutError->Reset();
    }
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    FString Error;
    FParsedValue Value;
    if (Variable.GetName().IsNone() || !Type.IsValid()
        || (!bAddIfMissing && Store.IndexOf(Variable) == INDEX_NONE)
        || !ParseParameterValue(Type, Text, Value, Error))
    {
        if (OutError)
        {
            *OutError = Error.IsEmpty() ? TEXT("invalid or missing Niagara parameter") : Error;
        }
        return false;
    }
    if (Store.IndexOf(Variable) != INDEX_NONE)
    {
        UObject* Existing = nullptr;
        const bool bStorageValid = Type.GetClass() != nullptr
            ? Storage::ReadObject(Store, Variable, Existing, Error)
            : Storage::ValidateBytes(Store, Variable, Variable.GetSizeInBytes(), Error);
        if (!bStorageValid)
        {
            if (OutError != nullptr)
            {
                *OutError = Error;
            }
            return false;
        }
    }
    const double* Components = Value.Components;
    if (Type == FNiagaraTypeDefinition::GetQuatDef())
    {
        return Store.SetParameterValue<FQuat4f>(FQuat4f(float(Components[0]), float(Components[1]),
            float(Components[2]), float(Components[3])), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        return Store.SetParameterValue<float>(float(Components[0]), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        return Store.SetParameterValue<int32>(Value.Integer, Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        return Store.SetParameterValue<FNiagaraBool>(FNiagaraBool(Value.Boolean), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        return Store.SetParameterValue<FVector2f>(FVector2f(float(Components[0]), float(Components[1])), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def())
    {
        return Store.SetParameterValue<FVector3f>(FVector3f(float(Components[0]), float(Components[1]), float(Components[2])), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        return Store.SetPositionParameterValue(FVector(Components[0], Components[1], Components[2]), Variable.GetName(), bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        return Store.SetParameterValue<FVector4f>(FVector4f(float(Components[0]), float(Components[1]), float(Components[2]), float(Components[3])), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        return Store.SetParameterValue<FLinearColor>(FLinearColor(float(Components[0]), float(Components[1]), float(Components[2]), float(Components[3])), Variable, bAddIfMissing);
    }
    if (Type.GetClass() != nullptr)
    {
        UObject* Object = Value.Object;
        if (Store.IndexOf(Variable) == INDEX_NONE)
        {
            if (!bAddIfMissing)
            {
                return false;
            }
            // The explicit value below owns initialization, including an intentional null.
            // Creating a default interface here wastes an object and requires a store owner.
            Store.AddParameter(Variable, false);
        }
        if (Type.IsDataInterface())
        {
            Store.SetDataInterface(Cast<UNiagaraDataInterface>(Object), Variable);
        }
        else
        {
            Store.SetUObject(Object, Variable);
        }
        return true;
    }
    return false;
}
}
