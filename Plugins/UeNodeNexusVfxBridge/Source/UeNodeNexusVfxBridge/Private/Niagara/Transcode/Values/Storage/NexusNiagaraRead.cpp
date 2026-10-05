#include "NexusNiagaraStorage.h"

#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraParameterStore.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraStorage, Log, All);

namespace UeNodeNexusBridge::VfxTranscode
{
template<typename T>
static bool Read(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, T& Value, FString& Error)
{
    return Storage::ReadBytes(Store, Variable, &Value, sizeof(T), Error);
}

static FString ReadText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, FString& Error)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        float Value;
        return Read(Store, Variable, Value, Error) ? Transcode::ExportFloat(Value) : FString();
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        int32 Value;
        return Read(Store, Variable, Value, Error) ? FString::FromInt(Value) : FString();
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        FNiagaraBool Value;
        if (!Read(Store, Variable, Value, Error))
        {
            return FString();
        }
        return Value.GetValue() ? TEXT("True") : TEXT("False");
    }
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        FVector2f Value;
        if (!Read(Store, Variable, Value, Error))
        {
            return FString();
        }
        return FString::Printf(TEXT("(X=%s,Y=%s)"), *Transcode::ExportFloat(Value.X), *Transcode::ExportFloat(Value.Y));
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        FVector3f Value;
        if (!Read(Store, Variable, Value, Error))
        {
            return FString();
        }
        return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"), *Transcode::ExportFloat(Value.X),
            *Transcode::ExportFloat(Value.Y), *Transcode::ExportFloat(Value.Z));
    }
    if (Type == FNiagaraTypeDefinition::GetQuatDef() || Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        // Both logical values contain four float32 fields. Copy into an aligned
        // local object; Niagara's packed storage need not share its alignment.
        FVector4f Value;
        if (!Read(Store, Variable, Value, Error))
        {
            return FString();
        }
        return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"), *Transcode::ExportFloat(Value.X),
            *Transcode::ExportFloat(Value.Y), *Transcode::ExportFloat(Value.Z), *Transcode::ExportFloat(Value.W));
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        FLinearColor Value;
        if (!Read(Store, Variable, Value, Error))
        {
            return FString();
        }
        return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"), *Transcode::ExportFloat(Value.R),
            *Transcode::ExportFloat(Value.G), *Transcode::ExportFloat(Value.B), *Transcode::ExportFloat(Value.A));
    }
    if (Type.IsValid() && Type.GetClass() != nullptr)
    {
        UObject* Object;
        if (!Storage::ReadObject(Store, Variable, Object, Error))
        {
            return FString();
        }
        return Object ? Object->GetPathName() : TEXT("None");
    }
    FString Text;
    return Storage::ReadStructText(Store, Variable, Text, Error) ? Text : FString();
}

FString ParameterValueText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, FString* OutError)
{
    FString Error;
    const FString Result = ReadText(Store, Variable, Error);
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
    if (!Error.IsEmpty())
    {
        UE_LOG(LogNexusNiagaraStorage, Error, TEXT("Niagara value read rejected: %s"), *Error);
    }
    return Result;
}
}
