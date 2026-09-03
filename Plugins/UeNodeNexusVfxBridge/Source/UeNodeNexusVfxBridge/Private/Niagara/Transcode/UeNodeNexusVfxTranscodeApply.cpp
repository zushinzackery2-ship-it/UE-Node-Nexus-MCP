#include "UeNodeNexusVfxTranscode.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusNiagaraOps.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;
using namespace VfxTranscode;

namespace
{
FString Str(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field, const FString& Default = FString())
{
    FString Value;
    return Op->TryGetStringField(Field, Value) ? Value : Default;
}

int32 EmitterIndex(UNiagaraSystem* System, const FString& Name)
{
    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
    for (int32 Index = 0; Index < Handles.Num(); ++Index)
    {
        if (Handles[Index].GetName().ToString().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

UNiagaraNodeOutput* StackOutput(UNiagaraSystem* System, int32 Emitter, const FString& Group)
{
    ENiagaraScriptUsage Usage;
    if (Emitter == INDEX_NONE || !UsageFromGroup(Group, Usage))
    {
        return nullptr;
    }
    return NiagaraModuleStack::ResolveOutputNode(&System->GetEmitterHandles()[Emitter], NiagaraModuleStack::UsageToString(Usage));
}

int32 ModuleIndex(UNiagaraNodeOutput* Output, const FString& Guid)
{
    TArray<UNiagaraNodeFunctionCall*> Modules;
    NiagaraModuleStack::GetOrderedModules(Output, Modules);
    for (int32 Index = 0; Index < Modules.Num(); ++Index)
    {
        if (Modules[Index]->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(Guid, ESearchCase::IgnoreCase))
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

FString ResolveScriptPath(const FString& Text)
{
    if (Text.StartsWith(TEXT("/")))
    {
        return Text;
    }
    for (TObjectIterator<UNiagaraScript> It; It; ++It)
    {
        if (It->Usage == ENiagaraScriptUsage::Module && It->GetName().Equals(Text, ESearchCase::IgnoreCase))
        {
            return It->GetPathName();
        }
    }
    return FString();
}

// Re-enter an existing Niagara handler with a synthesized payload; failures become plan failures.
bool Reenter(const TCHAR* Operation, TSharedPtr<FJsonObject> (*Handler)(const FString&, const FString&, const TSharedPtr<FJsonObject>&), const TSharedPtr<FJsonObject>& Payload, int32 Index, FApplyContext& Context, TSharedPtr<FJsonObject>& OutData)
{
    Payload->SetBoolField(TEXT("dry_run"), false);
    Payload->SetBoolField(TEXT("save"), false);
    TSharedPtr<FJsonObject> Response = Handler(Operation, TEXT("transcode"), Payload);
    if (!Response.IsValid() || !Response->GetBoolField(TEXT("ok")))
    {
        const TSharedPtr<FJsonObject>* Error = nullptr;
        FString Message = Operation;
        if (Response.IsValid() && Response->TryGetObjectField(TEXT("error"), Error) && Error != nullptr)
        {
            Message = (*Error)->GetStringField(TEXT("message"));
        }
        Context.Fail(Index, TEXT("apply_failed"), Message);
        return false;
    }
    const TSharedPtr<FJsonObject>* Data = nullptr;
    OutData = Response->TryGetObjectField(TEXT("data"), Data) && Data != nullptr ? *Data : MakeShared<FJsonObject>();
    Context.bChanged = true;
    return true;
}

TSharedPtr<FJsonObject> BasePayload(UNiagaraSystem* System, int32 Emitter)
{
    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), System->GetPathName());
    if (Emitter != INDEX_NONE)
    {
        Payload->SetNumberField(TEXT("emitter_index"), Emitter);
    }
    return Payload;
}


}

void VfxTranscode::ApplyNiagaraVerb(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const FString Verb = Str(Op, TEXT("op"));
    FString Error;
    TSharedPtr<FJsonObject> Data;
    if (Verb == TEXT("set_asset_prop"))
    {
        if (!ImportPropertyValue(System, Str(Op, TEXT("name")), Str(Op, TEXT("value")), Error)) { Context.Fail(Index, TEXT("prop_failed"), Error); return; }
        Context.bChanged = true;
        return;
    }
    if (Verb.StartsWith(TEXT("ns_user_param_")))
    {
        if (!ApplyUserParam(System, Verb, Op, Error)) { Context.Fail(Index, TEXT("user_param_failed"), Error); return; }
        Context.bChanged = true;
        return;
    }
    if (Verb == TEXT("ns_emitter_add"))
    {
        TSharedPtr<FJsonObject> Payload = BasePayload(System, INDEX_NONE);
        const FString Parent = Str(Op, TEXT("parent"));
        Payload->SetStringField(TEXT("mode"), Parent.IsEmpty() ? TEXT("default") : TEXT("from_asset"));
        Payload->SetStringField(TEXT("source_emitter_path"), Parent);
        Payload->SetStringField(TEXT("name"), Str(Op, TEXT("name")));
        Reenter(TEXT("niagara_emitter_create"), &HandleNiagaraEmitterCreate, Payload, Index, Context, Data);
        return;
    }
    const int32 Emitter = EmitterIndex(System, Str(Op, TEXT("emitter"), Str(Op, TEXT("name"))));
    if (Emitter == INDEX_NONE)
    {
        Context.Fail(Index, TEXT("emitter_not_found"), FString::Printf(TEXT("emitter not found: %s"), *Str(Op, TEXT("emitter"), Str(Op, TEXT("name")))));
        return;
    }
    if (Verb == TEXT("ns_emitter_remove"))
    {
        System->Modify();
        System->RemoveEmitterHandle(System->GetEmitterHandles()[Emitter]);
        Context.bChanged = true;
        return;
    }
    if (Verb == TEXT("ns_emitter_prop_set"))
    {
        if (!ApplyEmitterProp(System, Emitter, Str(Op, TEXT("name")), Str(Op, TEXT("value")), Error)) { Context.Fail(Index, TEXT("prop_failed"), Error); return; }
        Context.bChanged = true;
        return;
    }
    if (Verb.StartsWith(TEXT("ns_renderer_")))
    {
        const FString Id = Str(Op, TEXT("id"));
        if (Verb == TEXT("ns_renderer_add"))
        {
            TSharedPtr<FJsonObject> Payload = BasePayload(System, Emitter);
            FString Type = Str(Op, TEXT("class"));
            Type.RemoveFromStart(TEXT("Niagara"));
            Type.RemoveFromEnd(TEXT("RendererProperties"));
            Payload->SetStringField(TEXT("renderer_type"), Type.ToLower());
            if (Reenter(TEXT("niagara_renderer_create"), &HandleNiagaraRendererCreate, Payload, Index, Context, Data))
            {
                FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[Emitter].GetEmitterData();
                if (EmitterData && EmitterData->GetRenderers().Num() > 0)
                {
                    const FString Guid = EmitterData->GetRenderers().Last()->GetName();
                    Context.Ids.Add(Id, Guid);
                    Context.Created.Add(Id, Guid);
                }
            }
            return;
        }
        const FString* Guid = Context.Ids.Find(Id);
        UNiagaraRendererProperties* Renderer = Guid ? FindRenderer(System, Emitter, *Guid) : nullptr;
        if (Renderer == nullptr) { Context.Fail(Index, TEXT("renderer_not_found"), FString::Printf(TEXT("renderer not found: %s"), *Id)); return; }
        if (Verb == TEXT("ns_renderer_remove"))
        {
            FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Emitter];
            Handle.GetInstance().Emitter->RemoveRenderer(Renderer, Handle.GetInstance().Version);
            Context.bChanged = true;
            return;
        }
        if (!ImportPropertyValue(Renderer, Str(Op, TEXT("name")), Str(Op, TEXT("value")), Error)) { Context.Fail(Index, TEXT("prop_failed"), Error); return; }
        Context.bChanged = true;
        return;
    }
    const FString Group = Str(Op, TEXT("group"));
    UNiagaraNodeOutput* Output = StackOutput(System, Emitter, Group);
    ENiagaraScriptUsage Usage;
    if (Output == nullptr || !UsageFromGroup(Group, Usage)) { Context.Fail(Index, TEXT("stack_not_found"), FString::Printf(TEXT("stack not found: %s"), *Group)); return; }
    TSharedPtr<FJsonObject> Payload = BasePayload(System, Emitter);
    Payload->SetStringField(TEXT("usage"), NiagaraModuleStack::UsageToString(Usage));
    const FString Id = Str(Op, TEXT("id"));
    if (Verb == TEXT("ns_module_add"))
    {
        const FString Script = ResolveScriptPath(Str(Op, TEXT("script")));
        if (Script.IsEmpty()) { Context.Fail(Index, TEXT("unknown_module"), FString::Printf(TEXT("module script not found: %s"), *Str(Op, TEXT("script")))); return; }
        Payload->SetStringField(TEXT("module_script_path"), Script);
        int32 Target = INDEX_NONE;
        Op->TryGetNumberField(TEXT("index"), Target);
        Payload->SetNumberField(TEXT("target_index"), Target);
        if (Reenter(TEXT("niagara_module_add"), &HandleNiagaraModuleAdd, Payload, Index, Context, Data))
        {
            const FString Guid = Data->GetStringField(TEXT("node_guid"));
            Context.Ids.Add(Id, Guid);
            Context.Created.Add(Id, Guid);
        }
        return;
    }
    const FString* Guid = Context.Ids.Find(Id);
    const int32 Module = Guid ? ModuleIndex(Output, *Guid) : INDEX_NONE;
    if (Module == INDEX_NONE) { Context.Fail(Index, TEXT("module_not_found"), FString::Printf(TEXT("module not found: %s"), *Id)); return; }
    Payload->SetNumberField(TEXT("module_index"), Module);
    if (Verb == TEXT("ns_module_remove"))
    {
        Reenter(TEXT("niagara_module_remove"), &HandleNiagaraModuleRemove, Payload, Index, Context, Data);
    }
    else if (Verb == TEXT("ns_module_set_enabled"))
    {
        bool bEnabled = true;
        Op->TryGetBoolField(TEXT("enabled"), bEnabled);
        Payload->SetBoolField(TEXT("enabled"), bEnabled);
        Reenter(TEXT("niagara_module_set_enabled"), &HandleNiagaraModuleSetEnabled, Payload, Index, Context, Data);
    }
    else if (Verb == TEXT("ns_module_input_set") || Verb == TEXT("ns_module_input_reset"))
    {
        // Values the editor stored as rapid-iteration parameters are updated in place so the
        // stack UI keeps showing them; anything else goes through the override-pin path.
        TArray<UNiagaraNodeFunctionCall*> Modules;
        NiagaraModuleStack::GetOrderedModules(Output, Modules);
        FNiagaraVariable RapidVariable;
        UNiagaraScript* Script = nullptr;
        if (Modules.IsValidIndex(Module)
            && ResolveRapidIterationInput(System->GetEmitterHandles()[Emitter], Usage, Output->GetUsageId(), Modules[Module], Str(Op, TEXT("input")), RapidVariable, Script)
            && Script->RapidIterationParameters.IndexOf(RapidVariable) != INDEX_NONE)
        {
            Script->Modify();
            bool bOk = true;
            if (Verb == TEXT("ns_module_input_reset"))
            {
                Script->RapidIterationParameters.RemoveParameter(RapidVariable);
            }
            else
            {
                bOk = SetParameterValueText(Script->RapidIterationParameters, RapidVariable, Str(Op, TEXT("value")), false);
            }
            if (!bOk) { Context.Fail(Index, TEXT("input_failed"), FString::Printf(TEXT("could not parse %s for input %s"), *Str(Op, TEXT("value")), *Str(Op, TEXT("input")))); return; }
            NiagaraModuleStack::MarkSystemEdited(System);
            Context.bChanged = true;
            return;
        }
        TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
        Param->SetStringField(TEXT("name"), Str(Op, TEXT("input")));
        Param->SetStringField(TEXT("value_text"), Verb == TEXT("ns_module_input_set") ? Str(Op, TEXT("value")) : Str(Op, TEXT("default")));
        Payload->SetArrayField(TEXT("params"), { MakeShared<FJsonValueObject>(Param) });
        Reenter(TEXT("niagara_module_inputs_set"), &HandleNiagaraModuleInputsSet, Payload, Index, Context, Data);
    }
    else if (Verb == TEXT("ns_module_move"))
    {
        Context.Fail(Index, TEXT("unsupported_verb"), TEXT("module reordering is not supported in this version; remove and re-add the module"));
    }
    else
    {
        Context.Fail(Index, TEXT("unsupported_verb"), FString::Printf(TEXT("%s is not supported on Niagara systems"), *Verb));
    }
}
}

