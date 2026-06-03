#include "UeNodeNexusBridgeNiagaraModuleStack.h"

#include "UeNodeNexusBridgeJson.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge::NiagaraModuleStack
{
bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue)
{
    double Number = -1.0;
    if (!Payload->TryGetNumberField(Field, Number))
    {
        OutValue = INDEX_NONE;
        return false;
    }
    OutValue = static_cast<int32>(Number);
    return true;
}

FString UsageToString(ENiagaraScriptUsage Usage)
{
    const UEnum* Enum = StaticEnum<ENiagaraScriptUsage>();
    return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Usage)) : FString();
}

bool StringToUsage(const FString& Value, ENiagaraScriptUsage& OutUsage)
{
    if (Value.IsEmpty())
    {
        return false;
    }
    const UEnum* Enum = StaticEnum<ENiagaraScriptUsage>();
    const int64 Raw = Enum ? Enum->GetValueByNameString(Value) : INDEX_NONE;
    if (Raw == INDEX_NONE)
    {
        return false;
    }
    OutUsage = static_cast<ENiagaraScriptUsage>(Raw);
    return true;
}

FNiagaraEmitterHandle* ResolveEmitter(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    int32 EmitterIndex = INDEX_NONE;
    if (!ReadIndex(Payload, TEXT("emitter_index"), EmitterIndex) || !System->GetEmitterHandles().IsValidIndex(EmitterIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_emitter_index"), TEXT("emitter_index is required and must point to an existing emitter")));
        return nullptr;
    }
    return &System->GetEmitterHandles()[EmitterIndex];
}

UNiagaraGraph* ResolveEmitterGraph(FNiagaraEmitterHandle* Handle)
{
    FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
    UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
    return Source ? Source->NodeGraph : nullptr;
}

static bool IsParameterMapPin(UEdGraphPin* Pin)
{
    return Pin && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef();
}

static UEdGraphPin* FindParameterMapInputPin(UNiagaraNode* Node)
{
    if (!Node)
    {
        return nullptr;
    }
    FPinCollectorArray Pins;
    Node->GetInputPins(Pins);
    UEdGraphPin* const* Found = Pins.FindByPredicate([](UEdGraphPin* Pin)
    {
        return IsParameterMapPin(Pin);
    });
    return Found ? *Found : nullptr;
}

static UEdGraphPin* FindParameterMapOutputPin(UNiagaraNode* Node)
{
    if (!Node)
    {
        return nullptr;
    }
    FPinCollectorArray Pins;
    Node->GetOutputPins(Pins);
    UEdGraphPin* const* Found = Pins.FindByPredicate([](UEdGraphPin* Pin)
    {
        return IsParameterMapPin(Pin);
    });
    return Found ? *Found : nullptr;
}

void GetOrderedModules(UNiagaraNodeOutput* Output, TArray<UNiagaraNodeFunctionCall*>& OutModules)
{
    TSet<UNiagaraNode*> Visited;
    UNiagaraNode* Previous = Output;
    while (Previous && !Visited.Contains(Previous))
    {
        Visited.Add(Previous);
        UEdGraphPin* Input = FindParameterMapInputPin(Previous);
        if (!Input || Input->LinkedTo.Num() != 1)
        {
            return;
        }
        UNiagaraNode* Current = Cast<UNiagaraNode>(Input->LinkedTo[0]->GetOwningNode());
        if (UNiagaraNodeFunctionCall* Module = Cast<UNiagaraNodeFunctionCall>(Current))
        {
            OutModules.Insert(Module, 0);
        }
        Previous = Current;
    }
}

UNiagaraNodeOutput* ResolveOutputNode(FNiagaraEmitterHandle* Handle, const FString& UsageText)
{
    ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
    if (!StringToUsage(UsageText, Usage))
    {
        return nullptr;
    }
    UNiagaraGraph* Graph = ResolveEmitterGraph(Handle);
    if (!Graph)
    {
        return nullptr;
    }

    TArray<UNiagaraNodeOutput*> Outputs;
    Graph->GetNodesOfClass(Outputs);
    for (UNiagaraNodeOutput* Output : Outputs)
    {
        if (Output && Output->GetUsage() == Usage)
        {
            return Output;
        }
    }
    return nullptr;
}

UNiagaraNodeFunctionCall* ResolveModule(FNiagaraEmitterHandle* Handle, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    FString UsageText;
    int32 ModuleIndex = INDEX_NONE;
    if (!Payload->TryGetStringField(TEXT("usage"), UsageText) || !ReadIndex(Payload, TEXT("module_index"), ModuleIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("usage and module_index are required")));
        return nullptr;
    }
    UNiagaraNodeOutput* Output = ResolveOutputNode(Handle, UsageText);
    TArray<UNiagaraNodeFunctionCall*> Modules;
    GetOrderedModules(Output, Modules);
    if (!Modules.IsValidIndex(ModuleIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("module_not_found"), TEXT("module_index does not point to an existing module")));
        return nullptr;
    }
    return Modules[ModuleIndex];
}

TSharedPtr<FJsonObject> ModuleToJson(int32 EmitterIndex, UNiagaraNodeOutput* Output, int32 ModuleIndex, UNiagaraNodeFunctionCall* Module)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("emitter_index"), EmitterIndex);
    Json->SetStringField(TEXT("usage"), Output ? UsageToString(Output->GetUsage()) : FString());
    Json->SetNumberField(TEXT("module_index"), ModuleIndex);
    Json->SetStringField(TEXT("node_guid"), Module ? Module->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString());
    Json->SetStringField(TEXT("function_name"), Module ? Module->GetFunctionName() : FString());
    Json->SetStringField(TEXT("script_path"), Module && Module->FunctionScript ? Module->FunctionScript->GetPathName() : FString());
    Json->SetBoolField(TEXT("enabled"), Module ? Module->IsNodeEnabled() : false);
    return Json;
}

TSharedPtr<FJsonValue> ModuleToRow(int32 EmitterIndex, UNiagaraNodeOutput* Output, int32 ModuleIndex, UNiagaraNodeFunctionCall* Module)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueNumber>(EmitterIndex));
    Row.Add(MakeShared<FJsonValueString>(Output ? UsageToString(Output->GetUsage()) : FString()));
    Row.Add(MakeShared<FJsonValueNumber>(ModuleIndex));
    Row.Add(MakeShared<FJsonValueString>(Module ? Module->GetFunctionName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Module && Module->FunctionScript ? Module->FunctionScript->GetPathName() : FString()));
    Row.Add(MakeShared<FJsonValueBoolean>(Module ? Module->IsNodeEnabled() : false));
    return MakeShared<FJsonValueArray>(Row);
}

void MarkSystemEdited(UNiagaraSystem* System)
{
    if (System)
    {
        System->Modify();
        System->MarkPackageDirty();
        System->PostEditChange();
    }
}

bool RemoveModulePreservingStack(UNiagaraNodeFunctionCall* Module)
{
    UEdGraph* Graph = Module ? Module->GetGraph() : nullptr;
    UEdGraphPin* Input = Module ? FindParameterMapInputPin(Module) : nullptr;
    UEdGraphPin* Output = Module ? FindParameterMapOutputPin(Module) : nullptr;
    if (!Graph || !Input || !Output || Input->LinkedTo.Num() != 1 || Output->LinkedTo.Num() == 0)
    {
        return false;
    }

    UEdGraphPin* PreviousOutput = Input->LinkedTo[0];
    TArray<UEdGraphPin*> NextInputs = Output->LinkedTo;
    Graph->Modify();
    Module->Modify();
    Input->BreakAllPinLinks();
    Output->BreakAllPinLinks();
    for (UEdGraphPin* NextInput : NextInputs)
    {
        if (NextInput && PreviousOutput)
        {
            NextInput->BreakAllPinLinks();
            PreviousOutput->MakeLinkTo(NextInput);
        }
    }
    Graph->RemoveNode(Module);
    Graph->NotifyGraphChanged();
    return true;
}
}
