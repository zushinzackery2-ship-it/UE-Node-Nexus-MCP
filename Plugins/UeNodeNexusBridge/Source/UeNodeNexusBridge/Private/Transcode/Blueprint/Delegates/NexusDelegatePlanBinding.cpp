#include "NexusDelegatePlanBinding.h"

#include "Blueprint/Delegates/NexusDelegateBinding.h"
#include "K2Node_BaseMCDelegate.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

namespace UeNodeNexusBridge::Transcode
{
bool ValidateDelegatePlanBinding(UBlueprint* Blueprint, UClass* NodeClass,
    const TSharedPtr<FJsonObject>& Config, const FApplyContext& Context, FString& OutError)
{
    const FName Name(*ReadOpString(Config, TEXT("delegate_name")));
    const auto* Signature = Context.bDryRun && ReadOpString(Config, TEXT("delegate_owner")) == TEXT("self")
        ? Context.DispatcherSignatures.Find(Name) : nullptr;
    if (Signature == nullptr)
    {
        return ConfigureDelegateBinding(NodeClass->GetDefaultObject<UK2Node_BaseMCDelegate>(),
            Blueprint, Config, true, OutError);
    }
    if (!Signature->IsValid())
    {
        OutError = TEXT("dispatcher is removed by this plan: ") + Name.ToString();
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    if ((*Signature)->TryGetArrayField(TEXT("outputs"), Outputs) && !Outputs->IsEmpty())
    {
        OutError = TEXT("dispatcher cannot declare return parameters");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
    if (!(*Signature)->TryGetArrayField(TEXT("inputs"), Inputs))
    {
        OutError = TEXT("dispatcher signature needs an input array");
        return false;
    }
    TSet<FString> Names;
    for (const auto& Input : *Inputs)
    {
        const auto Parameter = Input.IsValid() && Input->Type == EJson::Object ? Input->AsObject() : nullptr;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        FString ParameterName;
        if (!Parameter.IsValid() || !Parameter->TryGetStringField(TEXT("name"), ParameterName)
            || ParameterName.IsEmpty() || Names.Contains(ParameterName)
            || !Parameter->TryGetObjectField(TEXT("type"), Type))
        {
            OutError = TEXT("dispatcher inputs need unique names and native types");
            return false;
        }
        FEdGraphPinType PinType;
        if (!PinTypeFromJson(*Type, PinType, OutError))
        {
            return false;
        }
        Names.Add(ParameterName);
    }
    return true;
}
}
