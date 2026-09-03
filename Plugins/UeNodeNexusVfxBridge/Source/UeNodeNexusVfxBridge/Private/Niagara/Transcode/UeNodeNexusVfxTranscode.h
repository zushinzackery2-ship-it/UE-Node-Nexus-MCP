#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "NiagaraCommon.h"

class UNiagaraEmitter;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
class UNiagaraRendererProperties;
class UNiagaraScript;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;
struct FNiagaraTypeDefinition;
struct FNiagaraVariable;
struct FNiagaraParameterStore;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleVfxTranscodeExport(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleVfxTranscodeApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);

namespace VfxTranscode
{
// Stack group name ("ParticleUpdate") <-> ENiagaraScriptUsage.
bool UsageFromGroup(const FString& Group, ENiagaraScriptUsage& OutUsage);
FString GroupFromUsage(ENiagaraScriptUsage Usage);
// Raw builders.
TSharedPtr<FJsonObject> BuildNiagaraSystemRaw(UNiagaraSystem* System);
TSharedPtr<FJsonObject> BuildNiagaraEmitterRaw(UNiagaraEmitter* Emitter);
TSharedPtr<FJsonObject> EmitterJson(FNiagaraEmitterHandle* Handle, UNiagaraEmitter* Emitter, const FGuid& Version);
// One stack module with its overridden inputs (override pins + rapid-iteration values).
TSharedPtr<FJsonObject> ModuleJson(FNiagaraEmitterHandle* Handle, UNiagaraNodeOutput* Output, UNiagaraNodeFunctionCall* Module);
// Parameter store value as import/export text.
FString ParameterValueText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable);
bool SetParameterValueText(FNiagaraParameterStore& Store, const FNiagaraVariable& Variable, const FString& Text, bool bAddIfMissing);
FNiagaraTypeDefinition TypeFromName(const FString& Name);
FString FriendlyTypeName(const FNiagaraTypeDefinition& Type);
// Schema files for the mirror (renderer classes are exported by the core module; this adds module signatures).
TSharedPtr<FJsonObject> BuildModuleSignatures();
// Local module-input values the editor stores as rapid-iteration parameters on the
// owning script (not as override pins). Resolves the store variable for one input;
// OutScript is the script whose RapidIterationParameters would hold it.
bool ResolveRapidIterationInput(FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage, const FGuid& UsageId, UNiagaraNodeFunctionCall* Module, const FString& InputName, FNiagaraVariable& OutVariable, UNiagaraScript*& OutScript);
}
}

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;
}

namespace UeNodeNexusBridge::VfxTranscode
{
// Applies one plan verb to a Niagara system (see transcode/plan.py for the verb table).
void ApplyNiagaraVerb(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Op, int32 Index, Transcode::FApplyContext& Context);
// Member helpers shared by the verb applier.
bool ApplyUserParam(UNiagaraSystem* System, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError);
bool ApplyEmitterProp(UNiagaraSystem* System, int32 Emitter, const FString& Name, const FString& Value, FString& OutError);
UNiagaraRendererProperties* FindRenderer(UNiagaraSystem* System, int32 Emitter, const FString& Guid);
}
