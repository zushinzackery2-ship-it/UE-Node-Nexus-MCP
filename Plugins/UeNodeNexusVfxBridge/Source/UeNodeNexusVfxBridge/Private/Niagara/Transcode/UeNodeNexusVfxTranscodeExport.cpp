#include "UeNodeNexusVfxTranscode.h"

#include "EdGraphUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::VfxTranscode
{
using namespace Transcode;

static TArray<TSharedPtr<FJsonValue>> StructProps(const UScriptStruct* Struct, const void* Data, const void* Defaults)
{
    TArray<TSharedPtr<FJsonValue>> Props;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsEditableProperty(Property))
        {
            continue;
        }
        FString Value;
        FString Default;
        Property->ExportTextItem_InContainer(Value, Data, nullptr, nullptr, PPF_None);
        if (Defaults != nullptr)
        {
            Property->ExportTextItem_InContainer(Default, Defaults, nullptr, nullptr, PPF_None);
        }
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Property->GetName());
        Json->SetStringField(TEXT("type"), Property->GetCPPType());
        Json->SetStringField(TEXT("value"), Value);
        Json->SetStringField(TEXT("default"), Default);
        Props.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Props;
}

// Attribute/parameter bindings serialize as registered type indices: meaningless in text.
static bool IsBindingProperty(const TSharedPtr<FJsonValue>& Prop)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    FString Type;
    FString Value;
    if (!Prop.IsValid() || !Prop->TryGetObject(Object))
    {
        return false;
    }
    (*Object)->TryGetStringField(TEXT("type"), Type);
    (*Object)->TryGetStringField(TEXT("value"), Value);
    return Type.Contains(TEXT("Binding")) || Value.Contains(TEXT("TypeDefHandle"));
}

static void AppendStacks(FNiagaraEmitterHandle* Handle, TArray<TSharedPtr<FJsonValue>>& Stacks, TArray<TSharedPtr<FJsonValue>>& Opaque)
{
    UNiagaraGraph* Graph = NiagaraModuleStack::ResolveEmitterGraph(Handle);
    if (Graph == nullptr)
    {
        return;
    }
    TArray<UNiagaraNodeOutput*> Outputs;
    Graph->GetNodesOfClass(Outputs);
    for (UNiagaraNodeOutput* Output : Outputs)
    {
        if (Output == nullptr)
        {
            continue;
        }
        TArray<UNiagaraNodeFunctionCall*> Modules;
        NiagaraModuleStack::GetOrderedModules(Output, Modules);
        const ENiagaraScriptUsage Usage = Output->GetUsage();
        ENiagaraScriptUsage Known;
        if (UsageFromGroup(GroupFromUsage(Usage), Known))
        {
            TSharedPtr<FJsonObject> Stack = MakeShared<FJsonObject>();
            Stack->SetStringField(TEXT("group"), GroupFromUsage(Usage));
            TArray<TSharedPtr<FJsonValue>> Rows;
            for (UNiagaraNodeFunctionCall* Module : Modules)
            {
                Rows.Add(MakeShared<FJsonValueObject>(ModuleJson(Handle, Output, Module)));
            }
            Stack->SetArrayField(TEXT("modules"), Rows);
            Stacks.Add(MakeShared<FJsonValueObject>(Stack));
            continue;
        }
        if (Modules.Num() == 0)
        {
            continue;
        }
        TSet<UObject*> Objects;
        for (UNiagaraNodeFunctionCall* Module : Modules)
        {
            Objects.Add(Module);
        }
        FString Text;
        FEdGraphUtilities::ExportNodesToText(Objects, Text);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        const FString Prefix = Usage == ENiagaraScriptUsage::ParticleEventScript ? TEXT("Event:") : (Usage == ENiagaraScriptUsage::ParticleSimulationStageScript ? TEXT("Stage:") : TEXT("Other:"));
        Item->SetStringField(TEXT("group"), Prefix + Output->GetUsageId().ToString(EGuidFormats::DigitsWithHyphens));
        Item->SetStringField(TEXT("t3d"), Text);
        Opaque.Add(MakeShared<FJsonValueObject>(Item));
    }
}

TSharedPtr<FJsonObject> EmitterJson(FNiagaraEmitterHandle* Handle, UNiagaraEmitter* Emitter, const FGuid& Version)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    FVersionedNiagaraEmitterData* Data = Emitter ? Emitter->GetEmitterData(Version) : nullptr;
    Json->SetStringField(TEXT("name"), Handle ? Handle->GetName().ToString() : (Emitter ? Emitter->GetName() : FString()));
    Json->SetStringField(TEXT("guid"), Handle ? Handle->GetId().ToString(EGuidFormats::DigitsWithHyphens) : FString());
    Json->SetBoolField(TEXT("enabled"), Handle ? Handle->GetIsEnabled() : true);
    const FVersionedNiagaraEmitter Parent = Data ? Data->GetParent() : FVersionedNiagaraEmitter();
    const bool bAssetParent = Parent.Emitter && Parent.Emitter->GetOutermost() != GetTransientPackage();
    Json->SetStringField(TEXT("parent"), bAssetParent ? Parent.Emitter->GetPathName() : FString());
    if (Data != nullptr)
    {
        FVersionedNiagaraEmitterData Defaults;
        Json->SetArrayField(TEXT("props"), StructProps(FVersionedNiagaraEmitterData::StaticStruct(), Data, &Defaults));
    }
    TArray<TSharedPtr<FJsonValue>> Stacks;
    TArray<TSharedPtr<FJsonValue>> Opaque;
    if (Handle != nullptr)
    {
        AppendStacks(Handle, Stacks, Opaque);
    }
    Json->SetArrayField(TEXT("stacks"), Stacks);
    Json->SetArrayField(TEXT("opaque_stacks"), Opaque);
    TArray<TSharedPtr<FJsonValue>> Renderers;
    const TArray<UNiagaraRendererProperties*> RendererList = Data ? Data->GetRenderers() : TArray<UNiagaraRendererProperties*>();
    for (UNiagaraRendererProperties* Renderer : RendererList)
    {
        if (Renderer == nullptr)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("guid"), Renderer->GetName());
        Item->SetStringField(TEXT("class"), Renderer->GetClass()->GetPathName());
        Item->SetStringField(TEXT("class_short"), Renderer->GetClass()->GetName());
        TArray<TSharedPtr<FJsonValue>> Props = ExportEditableProps(Renderer);
        Props.RemoveAll(IsBindingProperty);
        Item->SetArrayField(TEXT("props"), Props);
        Renderers.Add(MakeShared<FJsonValueObject>(Item));
    }
    Json->SetArrayField(TEXT("renderers"), Renderers);
    return Json;
}

TSharedPtr<FJsonObject> BuildNiagaraSystemRaw(UNiagaraSystem* System)
{
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(System, TEXT("niagara_system"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(System));
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> UserParams;
    const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    TArray<FNiagaraVariable> Variables;
    Store.GetParameters(Variables);
    for (const FNiagaraVariable& Variable : Variables)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        FString Name = Variable.GetName().ToString();
        Name.RemoveFromStart(TEXT("User."));
        Item->SetStringField(TEXT("name"), Name);
        Item->SetStringField(TEXT("type"), FriendlyTypeName(Variable.GetType()));
        Item->SetStringField(TEXT("value"), ParameterValueText(Store, Variable));
        UserParams.Add(MakeShared<FJsonValueObject>(Item));
    }
    Data->SetArrayField(TEXT("user_params"), UserParams);
    TArray<TSharedPtr<FJsonValue>> Emitters;
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
        Emitters.Add(MakeShared<FJsonValueObject>(EmitterJson(&Handle, Instance.Emitter, Instance.Version)));
    }
    Data->SetArrayField(TEXT("emitters"), Emitters);
    Raw->SetObjectField(TEXT("niagara"), Data);
    return Raw;
}

TSharedPtr<FJsonObject> BuildNiagaraEmitterRaw(UNiagaraEmitter* Emitter)
{
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Emitter, TEXT("niagara_emitter"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Emitter));
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("user_params"), TArray<TSharedPtr<FJsonValue>>());
    TArray<TSharedPtr<FJsonValue>> Emitters;
    Emitters.Add(MakeShared<FJsonValueObject>(EmitterJson(nullptr, Emitter, Emitter->GetExposedVersion().VersionGuid)));
    Data->SetArrayField(TEXT("emitters"), Emitters);
    Raw->SetObjectField(TEXT("niagara"), Data);
    return Raw;
}
}
