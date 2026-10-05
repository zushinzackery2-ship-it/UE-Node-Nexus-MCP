#include "UeNodeNexusVfxTranscode.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::VfxTranscode
{
using namespace Transcode;

static FString ReadField(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field)
{
    FString Value;
    Op->TryGetStringField(Field, Value);
    return Value;
}

bool ApplyUserParam(UNiagaraSystem* System, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(System) || !Op.IsValid())
    {
        OutError = TEXT("user parameter requires a valid Niagara system and operation");
        return false;
    }
    FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    FString Name = ReadField(Op, TEXT("name"));
    if (Name.IsEmpty() || Name == TEXT("User.") || FName(*Name).IsNone())
    {
        OutError = TEXT("user parameter requires a nonempty name");
        return false;
    }
    if (!Name.StartsWith(TEXT("User.")))
    {
        Name = TEXT("User.") + Name;
    }
    TArray<FNiagaraVariable> Variables;
    Store.GetParameters(Variables);
    FNiagaraVariable* Existing = Variables.FindByPredicate([&Name](const FNiagaraVariable& Item)
    {
        return Item.GetName().ToString() == Name;
    });
    if (Verb == TEXT("ns_user_param_remove"))
    {
        if (Existing != nullptr)
        {
            Store.RemoveParameter(*Existing);
        }
        return true;
    }
    const FNiagaraTypeDefinition Type = Existing ? Existing->GetType() : TypeFromName(ReadField(Op, TEXT("type")));
    const FString RequestedType = ReadField(Op, TEXT("type"));
    if (Existing && !RequestedType.IsEmpty() && !RequestedType.Equals(FriendlyTypeName(Type), ESearchCase::IgnoreCase)
        && TypeFromName(RequestedType) != Type)
    {
        OutError = FString::Printf(TEXT("user parameter %s already has type %s; requested %s"),
            *Name, *Type.GetName(), *ReadField(Op, TEXT("type")));
        return false;
    }
    if (!Type.IsValid())
    {
        OutError = FString::Printf(TEXT("unknown Niagara type: %s"), *ReadField(Op, TEXT("type")));
        return false;
    }
    FNiagaraVariable Variable(Type, FName(*Name));
    if (!SetParameterValueText(Store, Variable, ReadField(Op, TEXT("value")), true, &OutError))
    {
        OutError = FString::Printf(TEXT("user parameter %s: %s"), *Name, *OutError);
        return false;
    }
    return true;
}

bool ApplyEmitterProp(UNiagaraSystem* System, int32 Emitter, const FString& Name, const FString& Value, FString& OutError)
{
    if (!IsValid(System) || !System->GetEmitterHandles().IsValidIndex(Emitter))
    {
        OutError = TEXT("emitter index does not belong to the Niagara system");
        return false;
    }
    FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Emitter];
    if (Name == TEXT("Enabled"))
    {
        const FString Trimmed = Value.TrimStartAndEnd();
        return Handle.SetIsEnabled(Trimmed.StartsWith(TEXT("T"), ESearchCase::IgnoreCase) || Trimmed == TEXT("1"), *System, true);
    }
    FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
    FProperty* Property = Data ? FVersionedNiagaraEmitterData::StaticStruct()->FindPropertyByName(FName(*Name)) : nullptr;
    if (Property == nullptr || !IsEditableProperty(Property))
    {
        OutError = FString::Printf(TEXT("emitter has no editable property %s"), *Name);
        return false;
    }
    UNiagaraEmitter* EmitterAsset = Handle.GetInstance().Emitter;
    if (EmitterAsset != nullptr)
    {
        EmitterAsset->Modify();
    }
    if (Property->ImportText_InContainer(*Value, Data, nullptr, PPF_None) == nullptr)
    {
        OutError = FString::Printf(TEXT("ImportText failed for emitter property %s"), *Name);
        return false;
    }
    if (EmitterAsset != nullptr)
    {
        EmitterAsset->PostEditChange();
    }
    return true;
}

UNiagaraRendererProperties* FindRenderer(UNiagaraSystem* System, int32 Emitter, const FString& Guid)
{
    if (!IsValid(System) || !System->GetEmitterHandles().IsValidIndex(Emitter))
    {
        return nullptr;
    }
    FVersionedNiagaraEmitterData* Data = System->GetEmitterHandles()[Emitter].GetEmitterData();
    if (Data == nullptr)
    {
        return nullptr;
    }
    for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
    {
        if (Renderer && Renderer->GetName().Equals(Guid, ESearchCase::IgnoreCase))
        {
            return Renderer;
        }
    }
    return nullptr;
}
}
