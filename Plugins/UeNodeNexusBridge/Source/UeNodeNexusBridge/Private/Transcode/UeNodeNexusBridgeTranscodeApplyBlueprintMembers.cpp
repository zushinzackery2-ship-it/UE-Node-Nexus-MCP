#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "Blueprint/NexusBlueprintPinTypes.h"
#include "NexusBlueprintAssetType.h"
#include "Blueprint/Graphs/NexusGraphSchedule.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
TArray<FString> ReadOpStrings(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field)
{
    TArray<FString> Values;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (Op->TryGetArrayField(Field, Items) && Items != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            FString Text;
            if (Item.IsValid() && Item->TryGetString(Text))
            {
                Values.Add(Text);
            }
        }
    }
    return Values;
}

bool ReadOpPinType(const TSharedPtr<FJsonObject>& Op, FEdGraphPinType& OutType, FString& OutError)
{
    const TSharedPtr<FJsonObject>* Type = nullptr;
    if (!Op->TryGetObjectField(TEXT("type"), Type) || Type == nullptr)
    {
        OutError = TEXT("type is required");
        return false;
    }
    return PinTypeFromJson(*Type, OutType, OutError);
}

bool ApplyBlueprintMemberVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const bool bVariable = Verb.StartsWith(TEXT("bp_variable_"));
    const bool bComponent = Verb.StartsWith(TEXT("bp_component_"));
    const bool bFunction = Verb.StartsWith(TEXT("bp_function_")) || Verb == TEXT("bp_graph_add");
    const bool bLocal = Verb.StartsWith(TEXT("bp_local_variable_"));
    const bool bDefault = Verb == TEXT("bp_default_set");
    const bool bInterface = Verb.StartsWith(TEXT("bp_interface_"));
    const bool bDispatcher = Verb.StartsWith(TEXT("bp_dispatcher_"));
    if (!(bVariable || bComponent || bFunction || bLocal || bDefault || bInterface || bDispatcher))
    {
        return false;
    }
    if (Context.bDryRun)
    {
        return true;
    }
    FString Error;
    bool bOk = false;
    Blueprint->Modify();
    if (bVariable)
    {
        bOk = ApplyVariableVerb(Blueprint, Verb, Op, Error);
    }
    else if (bComponent)
    {
        bOk = ApplyComponentVerb(Blueprint, Verb, Op, Error);
    }
    else if (bFunction)
    {
        bOk = ApplyFunctionVerb(Blueprint, Verb, Op, Error);
    }
    else if (bLocal)
    {
        bOk = ApplyLocalVerb(Blueprint, Verb, Op, Error);
    }
    else if (bInterface)
    {
        bOk = ApplyInterfaceVerb(Blueprint, Verb, Op, Error);
    }
    else if (bDispatcher)
    {
        bOk = ApplyDispatcherVerb(Blueprint, Verb, Op, Error);
    }
    else
    {
        UObject* Cdo = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        bOk = Cdo != nullptr && ImportPropertyValue(Cdo, ReadOpString(Op, TEXT("name")), ReadOpString(Op, TEXT("value")), Error);
        if (Cdo == nullptr)
        {
            Error = TEXT("Blueprint has no generated class yet; compile it first");
        }
    }
    if (!bOk)
    {
        Context.Fail(Index, TEXT("member_failed"), Error.IsEmpty() ? FString::Printf(TEXT("%s failed"), *Verb) : Error);
        return true;
    }
    Context.bChanged = true;
    return true;
}

static void SettleGraphPinTypes(const TArray<UEdGraph*>& Graphs, FApplyContext& Context)
{
    if (Graphs.Num() == 0)
    {
        return;
    }
    const FWildcardResolution Resolution = ResolveWildcardPins(Graphs);
    if (Resolution.Remaining == 0)
    {
        return;
    }
    // Not a failure by itself: a macro graph's tunnel pins are wildcards by
    // design. The compile gate decides; this names the pins it would blame.
    Context.Note(TEXT("pin_type_unresolved"),
        FString::Printf(TEXT("%d linked pin(s) still have no type: %s"),
            Resolution.Remaining, *FString::Join(Resolution.Unresolved, TEXT(", "))));
}

static void ApplyAssetProp(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    FString Error;
    const FString Name = ReadOpString(Op, TEXT("name"));
    if (Name == TEXT("ParentClass"))
    {
        const FString Wanted = ReadOpString(Op, TEXT("value"));
        const UClass* Current = Blueprint->ParentClass;
        const bool bSame = Current != nullptr
            && (Current->GetPathName() == Wanted || Current->GetName() == Wanted || Current->GetName() == FPackageName::ObjectPathToObjectName(Wanted));
        if (!bSame)
        {
            Context.Fail(Index, TEXT("unsupported_verb"), TEXT("ParentClass cannot be changed from text; reparent in the editor"));
        }
    }
    else if (Name == TEXT("BlueprintType"))
    {
        // Carried by the mirror so a create knows what to build; on an
        // existing Blueprint the type is fixed and only ever verified.
        if (BlueprintTypeName(Blueprint) != ReadOpString(Op, TEXT("value")))
        {
            Context.Fail(Index, TEXT("unsupported_verb"), TEXT("BlueprintType is fixed at creation and cannot be changed from text"));
        }
    }
    else if (!Context.bDryRun && !ImportPropertyValue(Blueprint, Name, ReadOpString(Op, TEXT("value")), Error))
    {
        Context.Fail(Index, TEXT("prop_failed"), Error);
    }
    else
    {
        Context.bChanged |= !Context.bDryRun;
    }
}

// Verbs that change which members the skeleton class declares without regenerating
// it. Call nodes resolve their function, and variable nodes their property, on the
// skeleton class; a component is the member variable of its SCS node. Variable
// verbs and component renames regenerate the skeleton inside the editor utility
// they call, raw SCS node edits do not.
static bool DeclaresMember(const FString& Verb)
{
    return Verb == TEXT("bp_function_add") || Verb == TEXT("bp_function_signature_set") || Verb == TEXT("bp_function_rename")
        || Verb == TEXT("bp_component_add") || Verb == TEXT("bp_component_remove")
        || Verb.StartsWith(TEXT("bp_interface_")) || Verb.StartsWith(TEXT("bp_dispatcher_"));
}

void ApplyBlueprintPlan(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    if (Blueprint == nullptr)
    {
        Context.Fail(INDEX_NONE, TEXT("invalid_asset"), TEXT("asset is not a Blueprint"));
        return;
    }
    // Declarations run before any graph verb: a node may call a function or read a
    // variable that the same plan declares later, including across graphs.
    TArray<int32> GraphOps;
    TSet<FName> ChangedFunctions;
    bool bMembersDeclared = false;
    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Op = Plan[Index].IsValid() ? Plan[Index]->AsObject() : nullptr;
        if (!Op.IsValid())
        {
            Context.Fail(Index, TEXT("invalid_verb"), TEXT("plan entries must be objects"));
            continue;
        }
        const FString Verb = ReadOpString(Op, TEXT("op"));
        if (Verb == TEXT("bp_function_signature_set"))
        {
            ChangedFunctions.Add(FName(*ReadOpString(Op, TEXT("name"))));
        }
        if (Verb == TEXT("set_asset_prop"))
        {
            ApplyAssetProp(Blueprint, Op, Index, Context);
            continue;
        }
        if (ApplyBlueprintMemberVerb(Blueprint, Verb, Op, Index, Context))
        {
            bMembersDeclared |= DeclaresMember(Verb);
            continue;
        }
        GraphOps.Add(Index);
    }
    if (bMembersDeclared && !Context.bDryRun)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        RefreshFunctionCalls(Blueprint, ChangedFunctions);
    }
    TArray<UEdGraph*> Touched;
    ApplyScheduledGraphOps(Blueprint, Plan, GraphOps, Context, Touched);
    if (Context.bChanged && !Context.bDryRun)
    {
        SettleGraphPinTypes(Touched, Context);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
}
}
