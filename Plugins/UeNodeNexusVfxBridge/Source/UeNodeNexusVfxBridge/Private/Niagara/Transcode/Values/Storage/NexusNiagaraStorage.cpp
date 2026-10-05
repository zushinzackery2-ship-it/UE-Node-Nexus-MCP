#include "NexusNiagaraStorage.h"

#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"

namespace UeNodeNexusBridge::VfxTranscode::Storage
{
static bool Reject(const FNiagaraVariable& Variable, FString& Error, const FString& Reason)
{
    Error = FString::Printf(TEXT("parameter=%s type=%s: %s"), *Variable.GetName().ToString(),
        *(Variable.GetType().IsValid() ? Variable.GetType().GetName() : FString(TEXT("invalid"))), *Reason);
    return false;
}

bool ValidateBytes(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    int32 DestinationSize, FString& Error)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    if (!Type.IsValid() || Type.GetClass() != nullptr
        || DestinationSize <= 0 || Variable.GetSizeInBytes() != DestinationSize)
    {
        return Reject(Variable, Error, TEXT("value storage category or destination size does not match"));
    }
    const FNiagaraVariableWithOffset* Stored = Store.FindParameterVariable(Variable);
    if (Stored == nullptr)
    {
        return Reject(Variable, Error, TEXT("parameter is absent from the value store"));
    }
    const int32 SourceSize = Stored->StructConverter.IsValid()
        ? FNiagaraTypeHelper::GetSWCType(Type).GetSize() : DestinationSize;
    const int32 Available = Store.GetParameterDataArray().Num();
    if (SourceSize <= 0 || Stored->Offset < 0 || Stored->Offset > Available
        || SourceSize > Available - Stored->Offset)
    {
        return Reject(Variable, Error, FString::Printf(TEXT("value range exceeds storage: offset=%d size=%d bytes=%d"),
            Stored->Offset, SourceSize, Available));
    }
    return true;
}

bool ReadBytes(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    void* Destination, int32 DestinationSize, FString& Error)
{
    if (Destination == nullptr)
    {
        return Reject(Variable, Error, TEXT("value copy requires a destination"));
    }
    if (!ValidateBytes(Store, Variable, DestinationSize, Error))
    {
        return false;
    }
    if (!Store.CopyParameterData(Variable, static_cast<uint8*>(Destination)))
    {
        return Reject(Variable, Error, TEXT("engine could not copy the verified value"));
    }
    return true;
}

bool ReadObject(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    UObject*& Object, FString& Error)
{
    Object = nullptr;
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    if (!Type.IsValid() || Type.GetClass() == nullptr)
    {
        return Reject(Variable, Error, TEXT("object storage requires an object or data interface type"));
    }
    const int32 Index = Store.IndexOf(Variable);
    const bool bInterface = Type.IsDataInterface();
    const int32 Count = bInterface ? Store.GetDataInterfaces().Num() : Store.GetUObjects().Num();
    if (Index < 0 || Index >= Count)
    {
        return Reject(Variable, Error, FString::Printf(TEXT("object storage index exceeds its category: index=%d count=%d"), Index, Count));
    }
    Object = bInterface ? static_cast<UObject*>(Store.GetDataInterfaces()[Index]) : Store.GetUObjects()[Index];
    if (Object != nullptr && (!IsValid(Object) || !Object->IsA(Type.GetClass())))
    {
        return Reject(Variable, Error, TEXT("stored object is invalid or has a different class"));
    }
    return true;
}
}
