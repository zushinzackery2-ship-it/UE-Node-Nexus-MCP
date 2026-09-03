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
    FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    FString Name = ReadField(Op, TEXT("name"));
    if (!Name.StartsWith(TEXT("User.")))
    {
        Name = TEXT("User.") + Name;
    }
    TArray<FNiagaraVariable> Variables;
    Store.GetParameters(Variables);
    FNiagaraVariable* Existing = Variables.FindByPredicate([&Name](const FNiagaraVariable& Item) { return Item.GetName().ToString() == Name; });
    if (Verb == TEXT("ns_user_param_remove"))
    {
        if (Existing != nullptr)
        {
            Store.RemoveParameter(*Existing);
        }
        return true;
    }
    const FNiagaraTypeDefinition Type = Existing ? Existing->GetType() : TypeFromName(ReadField(Op, TEXT("type")));
    if (!Type.IsValid())
    {
        OutError = FString::Printf(TEXT("unknown Niagara type: %s"), *ReadField(Op, TEXT("type")));
        return false;
    }
    FNiagaraVariable Variable(Type, FName(*Name));
    if (!SetParameterValueText(Store, Variable, ReadField(Op, TEXT("value")), true))
    {
        OutError = FString::Printf(TEXT("could not set user parameter %s = %s"), *Name, *ReadField(Op, TEXT("value")));
        return false;
    }
    return true;
}

bool ApplyEmitterProp(UNiagaraSystem* System, int32 Emitter, const FString& Name, const FString& Value, FString& OutError)
{
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
