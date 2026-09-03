#include "UeNodeNexusVfxTranscode.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

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

FString ParameterValueText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        return FString::SanitizeFloat(Store.GetParameterValue<float>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        return FString::FromInt(Store.GetParameterValue<int32>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        return Store.GetParameterValue<FNiagaraBool>(Variable).GetValue() ? TEXT("True") : TEXT("False");
    }
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        const FVector2f Value = Store.GetParameterValue<FVector2f>(Variable);
        return FString::Printf(TEXT("(X=%s,Y=%s)"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y));
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        const FVector3f Value = Store.GetParameterValue<FVector3f>(Variable);
        return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y), *FString::SanitizeFloat(Value.Z));
    }
    if (Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        const FVector4f Value = Store.GetParameterValue<FVector4f>(Variable);
        return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y), *FString::SanitizeFloat(Value.Z), *FString::SanitizeFloat(Value.W));
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        return Store.GetParameterValue<FLinearColor>(Variable).ToString();
    }
    if (Type.GetClass() != nullptr)
    {
        UObject* Object = Store.GetUObject(Variable);
        return Object ? Object->GetPathName() : TEXT("None");
    }
    return FString();
}

// Accepts both "(X=1,Y=2,Z=3)" (export text) and Niagara's pin form "1.000,2.000,3.000".
static bool ParseComponents(const FString& Text, const TCHAR* const* Keys, int32 Count, float* Out)
{
    if (!Text.Contains(TEXT("=")))
    {
        TArray<FString> Parts;
        Text.TrimStartAndEnd().TrimChar('(').TrimChar(')').ParseIntoArray(Parts, TEXT(","), true);
        if (Parts.Num() < Count)
        {
            return false;
        }
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Out[Index] = FCString::Atof(*Parts[Index].TrimStartAndEnd());
        }
        return true;
    }
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FString Value;
        if (!FParse::Value(*Text, Keys[Index], Value))
        {
            return false;
        }
        Out[Index] = FCString::Atof(*Value);
    }
    return true;
}

bool SetParameterValueText(FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, const FString& Text, bool bAddIfMissing)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    float Components[4] = { 0.f, 0.f, 0.f, 1.f };
    static const TCHAR* const XYZW[] = { TEXT("X="), TEXT("Y="), TEXT("Z="), TEXT("W=") };
    static const TCHAR* const RGBA[] = { TEXT("R="), TEXT("G="), TEXT("B="), TEXT("A=") };
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        return Store.SetParameterValue<float>(FCString::Atof(*Text), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        return Store.SetParameterValue<int32>(FCString::Atoi(*Text), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        return Store.SetParameterValue<FNiagaraBool>(FNiagaraBool(Text.TrimStartAndEnd().StartsWith(TEXT("T"), ESearchCase::IgnoreCase) || Text.TrimStartAndEnd() == TEXT("1")), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        return ParseComponents(Text, XYZW, 2, Components) && Store.SetParameterValue<FVector2f>(FVector2f(Components[0], Components[1]), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def())
    {
        return ParseComponents(Text, XYZW, 3, Components) && Store.SetParameterValue<FVector3f>(FVector3f(Components[0], Components[1], Components[2]), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        return ParseComponents(Text, XYZW, 3, Components) && Store.SetPositionParameterValue(FVector(Components[0], Components[1], Components[2]), Variable.GetName(), bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        return ParseComponents(Text, XYZW, 4, Components) && Store.SetParameterValue<FVector4f>(FVector4f(Components[0], Components[1], Components[2], Components[3]), Variable, bAddIfMissing);
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        return ParseComponents(Text, RGBA, 4, Components) && Store.SetParameterValue<FLinearColor>(FLinearColor(Components[0], Components[1], Components[2], Components[3]), Variable, bAddIfMissing);
    }
    if (Type.GetClass() != nullptr)
    {
        UObject* Object = Text.IsEmpty() || Text == TEXT("None") ? nullptr : ResolveObjectByPath(Text);
        if (Object != nullptr && !Object->IsA(Type.GetClass()))
        {
            return false;
        }
        if (Store.IndexOf(Variable) == INDEX_NONE)
        {
            if (!bAddIfMissing)
            {
                return false;
            }
            Store.AddParameter(Variable);
        }
        Store.SetUObject(Object, Variable);
        return true;
    }
    return false;
}
}
