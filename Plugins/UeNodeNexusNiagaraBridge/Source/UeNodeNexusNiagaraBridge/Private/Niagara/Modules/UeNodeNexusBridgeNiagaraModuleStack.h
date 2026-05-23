#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class FJsonObject;
class FJsonValue;
class UNiagaraGraph;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;

namespace UeNodeNexusBridge::NiagaraModuleStack
{
bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue);
FString UsageToString(ENiagaraScriptUsage Usage);
bool StringToUsage(const FString& Value, ENiagaraScriptUsage& OutUsage);

FNiagaraEmitterHandle* ResolveEmitter(
    UNiagaraSystem* System,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FJsonObject>& OutError,
    const FString& Operation,
    const FString& RequestId);

UNiagaraGraph* ResolveEmitterGraph(FNiagaraEmitterHandle* Handle);
UNiagaraNodeOutput* ResolveOutputNode(FNiagaraEmitterHandle* Handle, const FString& UsageText);

UNiagaraNodeFunctionCall* ResolveModule(
    FNiagaraEmitterHandle* Handle,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FJsonObject>& OutError,
    const FString& Operation,
    const FString& RequestId);

void GetOrderedModules(UNiagaraNodeOutput* Output, TArray<UNiagaraNodeFunctionCall*>& OutModules);
TSharedPtr<FJsonObject> ModuleToJson(int32 EmitterIndex, UNiagaraNodeOutput* Output, int32 ModuleIndex, UNiagaraNodeFunctionCall* Module);
TSharedPtr<FJsonValue> ModuleToRow(int32 EmitterIndex, UNiagaraNodeOutput* Output, int32 ModuleIndex, UNiagaraNodeFunctionCall* Module);
void MarkSystemEdited(UNiagaraSystem* System);
bool RemoveModulePreservingStack(UNiagaraNodeFunctionCall* Module);
}
