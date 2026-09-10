#include "UeNodeNexusBridgeOperations.h"
#include "UeNodeNexusBridgeBuildInfo.h"

#include "Algo/Sort.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperationRegistry.h"
#include "UeNodeNexusBridgeTranscode.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonValue> MakeStringValue(const FString& Value)
{
    return MakeShared<FJsonValueString>(Value);
}

static TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(Values.Num());
    for (const FString& Value : Values)
    {
        Items.Add(MakeStringValue(Value));
    }
    return Items;
}

static TArray<FString> MergeOperationNames(const TArray<FString>& Core, const TArray<FString>& AutoIndex, const TArray<FString>& Registered)
{
    TSet<FString> Seen;
    TArray<FString> Merged;
    auto AddUnique = [&Seen, &Merged](const TArray<FString>& Operations)
    {
        for (const FString& Operation : Operations)
        {
            if (!Seen.Contains(Operation))
            {
                Seen.Add(Operation);
                Merged.Add(Operation);
            }
        }
    };

    AddUnique(Core);
    AddUnique(AutoIndex);
    AddUnique(Registered);
    Algo::Sort(Merged);
    return Merged;
}

static TArray<FString> ReadStringArrayField(const TSharedPtr<FJsonObject>& Payload, const FString& FieldName)
{
    TArray<FString> Operations;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Payload.IsValid() || !Payload->TryGetArrayField(FieldName, Values) || Values == nullptr)
    {
        return Operations;
    }

    TSet<FString> Seen;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Operation;
        if (Value.IsValid() && Value->TryGetString(Operation) && !Operation.IsEmpty() && !Seen.Contains(Operation))
        {
            Seen.Add(Operation);
            Operations.Add(Operation);
        }
    }
    Algo::Sort(Operations);
    return Operations;
}

static TSet<FString> MakeStringSet(const TArray<FString>& Values)
{
    TSet<FString> Items;
    for (const FString& Value : Values)
    {
        Items.Add(Value);
    }
    return Items;
}

static TArray<FString> Difference(const TArray<FString>& Left, const TSet<FString>& RightSet)
{
    TArray<FString> Items;
    for (const FString& Value : Left)
    {
        if (!RightSet.Contains(Value))
        {
            Items.Add(Value);
        }
    }
    Algo::Sort(Items);
    return Items;
}

TSharedPtr<FJsonObject> HandleBridgeCapabilitiesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TArray<FString> CoreOperations = GetCoreOperationNames();
    TArray<FString> AutoIndexOperations = GetAutoIndexOperationNames();
    Algo::Sort(CoreOperations);
    Algo::Sort(AutoIndexOperations);

    // Core operations now dispatch through the shared registry as well, so
    // GetRegisteredOperations() includes them. Subtract the statically-named
    // core and AutoIndex operations so the "registered" bucket keeps reporting
    // only plugin-contributed operations (e.g. Niagara). The merged total below
    // is unaffected because MergeOperationNames de-duplicates.
    TSet<FString> StaticallyNamedOperations = MakeStringSet(CoreOperations);
    StaticallyNamedOperations.Append(MakeStringSet(AutoIndexOperations));
    const TArray<FString> RegisteredOperations = Difference(GetRegisteredOperations(), StaticallyNamedOperations);

    const TArray<FString> AllOperations = MergeOperationNames(CoreOperations, AutoIndexOperations, RegisteredOperations);
    const TArray<FString> ExpectedOperations = ReadStringArrayField(Payload, TEXT("expected_operations"));
    const TArray<FString> AllowedExtraOperations = ReadStringArrayField(Payload, TEXT("allowed_extra_operations"));
    const TSet<FString> AllOperationSet = MakeStringSet(AllOperations);
    const TSet<FString> ExpectedOperationSet = MakeStringSet(ExpectedOperations);
    const TSet<FString> AllowedExtraOperationSet = MakeStringSet(AllowedExtraOperations);
    TSet<FString> ExpectedOrAllowedSet = ExpectedOperationSet;
    ExpectedOrAllowedSet.Append(AllowedExtraOperationSet);

    const TArray<FString> MissingExpectedOperations = Difference(ExpectedOperations, AllOperationSet);
    const TArray<FString> RawExtraBridgeOperations = Difference(AllOperations, ExpectedOperationSet);
    const TArray<FString> UnexpectedExtraOperations = Difference(AllOperations, ExpectedOrAllowedSet);

    TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
    Counts->SetNumberField(TEXT("core"), CoreOperations.Num());
    Counts->SetNumberField(TEXT("auto_index"), AutoIndexOperations.Num());
    Counts->SetNumberField(TEXT("registered"), RegisteredOperations.Num());
    Counts->SetNumberField(TEXT("total"), AllOperations.Num());
    Counts->SetNumberField(TEXT("expected"), ExpectedOperations.Num());
    Counts->SetNumberField(TEXT("allowed_extra"), AllowedExtraOperations.Num());
    Counts->SetNumberField(TEXT("missing_expected"), MissingExpectedOperations.Num());
    Counts->SetNumberField(TEXT("extra_bridge"), ExpectedOperations.IsEmpty() ? 0 : UnexpectedExtraOperations.Num());
    Counts->SetNumberField(TEXT("raw_extra_bridge"), ExpectedOperations.IsEmpty() ? 0 : RawExtraBridgeOperations.Num());

    TSharedPtr<FJsonObject> Modules = MakeShared<FJsonObject>();
    const TSharedPtr<IPlugin> NiagaraPlugin = IPluginManager::Get().FindPlugin(TEXT("Niagara"));
    const TSharedPtr<IPlugin> VfxBridgePlugin = IPluginManager::Get().FindPlugin(TEXT("UeNodeNexusVfxBridge"));
    const bool bCoreLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("UeNodeNexusBridge"));
    const bool bVfxLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("UeNodeNexusVfxBridge"));
    const bool bNiagaraPluginEnabled = NiagaraPlugin.IsValid() && NiagaraPlugin->IsEnabled();
    const bool bVfxBridgePluginEnabled = VfxBridgePlugin.IsValid() && VfxBridgePlugin->IsEnabled();
    Modules->SetBoolField(TEXT("core_loaded"), bCoreLoaded);
    Modules->SetBoolField(TEXT("vfx_bridge_loaded"), bVfxLoaded);
    Modules->SetBoolField(TEXT("niagara_plugin_enabled"), bNiagaraPluginEnabled);
    Modules->SetBoolField(TEXT("vfx_bridge_plugin_enabled"), bVfxBridgePluginEnabled);
    Modules->SetBoolField(TEXT("vfx_available"), bNiagaraPluginEnabled && bVfxBridgePluginEnabled && bVfxLoaded);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("bridge"), TEXT("UeNodeNexusBridge"));
    Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Data->SetArrayField(TEXT("operations"), MakeStringArray(AllOperations));
    Data->SetArrayField(TEXT("core_operations"), MakeStringArray(CoreOperations));
    Data->SetArrayField(TEXT("auto_index_operations"), MakeStringArray(AutoIndexOperations));
    Data->SetArrayField(TEXT("registered_operations"), MakeStringArray(RegisteredOperations));
    Data->SetArrayField(TEXT("expected_operations"), MakeStringArray(ExpectedOperations));
    Data->SetArrayField(TEXT("allowed_extra_operations"), MakeStringArray(AllowedExtraOperations));
    Data->SetArrayField(TEXT("missing_expected_operations"), MakeStringArray(MissingExpectedOperations));
    Data->SetArrayField(TEXT("extra_bridge_operations"), MakeStringArray(ExpectedOperations.IsEmpty() ? TArray<FString>() : UnexpectedExtraOperations));
    Data->SetArrayField(TEXT("raw_extra_bridge_operations"), MakeStringArray(ExpectedOperations.IsEmpty() ? TArray<FString>() : RawExtraBridgeOperations));
    Data->SetBoolField(TEXT("contract_ok"), ExpectedOperations.IsEmpty() || (MissingExpectedOperations.IsEmpty() && UnexpectedExtraOperations.IsEmpty()));
    Data->SetObjectField(TEXT("counts"), Counts);
    Data->SetObjectField(TEXT("modules"), Modules);
    Data->SetObjectField(TEXT("build"), BridgeBuildIdentities());
    Data->SetNumberField(TEXT("contract_version"), NEXUS_CONTRACT_VERSION);
    Data->SetStringField(TEXT("schema_key"), Transcode::SchemaKey());
    Data->SetStringField(TEXT("engine_version"), Transcode::EngineVersionString());
    Data->SetStringField(TEXT("transcode_root"), Transcode::GetMirrorRoot());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
