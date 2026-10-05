#include "NexusNiagaraValue.h"

#include "UeNodeNexusBridgeObjectHelpers.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge::VfxTranscode
{
static bool Number(const FString& Text, double& Out)
{
    const FString Trimmed = Text.TrimStartAndEnd();
    return !Trimmed.IsEmpty() && FDefaultValueHelper::ParseDouble(Trimmed, Out) && FMath::IsFinite(Out);
}

static bool Components(const FString& Text, int32 Count, bool bColor, double* Out)
{
    FString Body = Text.TrimStartAndEnd();
    if (Body.StartsWith(TEXT("(")) && Body.EndsWith(TEXT(")")))
    {
        Body = Body.Mid(1, Body.Len() - 2);
    }
    TArray<FString> Parts;
    Body.ParseIntoArray(Parts, TEXT(","), false);
    if (Parts.Num() != Count)
    {
        return false;
    }
    const TCHAR* Keys = bColor ? TEXT("RGBA") : TEXT("XYZW");
    TSet<int32> Seen;
    const bool bNamed = Body.Contains(TEXT("="));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FString Value = Parts[Index].TrimStartAndEnd();
        int32 Slot = Index;
        if (bNamed)
        {
            FString Key;
            if (!Value.Split(TEXT("="), &Key, &Value))
            {
                return false;
            }
            Key = Key.TrimStartAndEnd().ToUpper();
            Slot = Key.Len() == 1 ? FString(Keys).Find(Key) : INDEX_NONE;
            if (Slot == INDEX_NONE || Slot >= Count || Seen.Contains(Slot))
            {
                return false;
            }
        }
        Seen.Add(Slot);
        if (!Number(Value, Out[Slot]))
        {
            return false;
        }
    }
    return true;
}

bool ParseParameterValue(const FNiagaraTypeDefinition& Type, const FString& Text, FParsedValue& Out, FString& Error)
{
    const FString Trimmed = Text.TrimStartAndEnd();
    bool bOk = false;
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        TCHAR* End = nullptr;
        const int64 Value = FCString::Strtoi64(*Trimmed, &End, 10);
        bOk = !Trimmed.IsEmpty() && End && *End == 0 && Value >= MIN_int32 && Value <= MAX_int32;
        Out.Integer = bOk ? int32(Value) : 0;
    }
    else if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        bOk = Trimmed.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Trimmed == TEXT("1")
            || Trimmed.Equals(TEXT("False"), ESearchCase::IgnoreCase) || Trimmed == TEXT("0");
        Out.Boolean = Trimmed.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Trimmed == TEXT("1");
    }
    else if (Type.GetClass())
    {
        if (!Trimmed.IsEmpty() && Trimmed != TEXT("None"))
        {
            Out.Object = FindObject<UObject>(nullptr, *Trimmed);
            const FSoftObjectPath Path(Trimmed);
            if (!Out.Object && Path.IsValid() && FPackageName::DoesPackageExist(Path.GetLongPackageName()))
            {
                Out.Object = ResolveObjectByPath(Trimmed);
            }
        }
        bOk = (Trimmed.IsEmpty() || Trimmed == TEXT("None") || IsValid(Out.Object))
            && (!Out.Object || Out.Object->IsA(Type.GetClass()));
    }
    else
    {
        const int32 Count = Type == FNiagaraTypeDefinition::GetFloatDef() ? 1
            : Type == FNiagaraTypeDefinition::GetVec2Def() ? 2
            : Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef() ? 3
            : Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetQuatDef()
                || Type == FNiagaraTypeDefinition::GetColorDef() ? 4 : 0;
        bOk = Count == 1 ? Number(Trimmed, Out.Components[0])
            : Count > 0 && Components(Trimmed, Count, Type == FNiagaraTypeDefinition::GetColorDef(), Out.Components);
        for (int32 Index = 0; bOk && Index < Count; ++Index)
        {
            bOk = Type == FNiagaraTypeDefinition::GetPositionDef() || FMath::IsFinite(float(Out.Components[Index]));
        }
    }
    if (!bOk)
    {
        Error = FString::Printf(TEXT("invalid value '%s' for Niagara type %s"), *Text, *Type.GetName());
    }
    return bOk;
}
}
