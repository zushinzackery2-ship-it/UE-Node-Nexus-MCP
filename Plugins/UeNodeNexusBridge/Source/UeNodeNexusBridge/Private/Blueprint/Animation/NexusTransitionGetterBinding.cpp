#include "NexusTransitionGetterBinding.h"

#include "AnimationTransitionSchema.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_TransitionRuleGetter.h"

namespace UeNodeNexusBridge
{
bool ConfigureTransitionGetter(UK2Node_TransitionRuleGetter* Node, const UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError)
{
    if (!Node || !Graph || !Graph->GetSchema() || !Graph->GetSchema()->IsA<UAnimationTransitionSchema>())
    {
        OutError = TEXT("transition getter requires an animation transition rule graph");
        return false;
    }
    FString Name;
    if (!Payload->TryGetStringField(TEXT("getter_type"), Name))
    {
        OutError = TEXT("getter_type is required for K2Node_TransitionRuleGetter");
        return false;
    }
    const int64 Value = StaticEnum<ETransitionGetter::Type>()->GetValueByNameString(Name);
    if (Value != ETransitionGetter::CurrentState_ElapsedTime
        && Value != ETransitionGetter::CurrentState_GetBlendWeight
        && Value != ETransitionGetter::CurrentTransitionDuration)
    {
        OutError = TEXT("getter_type must be CurrentState_ElapsedTime, "
            "CurrentState_GetBlendWeight or CurrentTransitionDuration; asset/state getters require explicit source binding");
        return false;
    }
    if (!bDryRun)
    {
        Node->GetterType = static_cast<ETransitionGetter::Type>(Value);
    }
    return true;
}
}
