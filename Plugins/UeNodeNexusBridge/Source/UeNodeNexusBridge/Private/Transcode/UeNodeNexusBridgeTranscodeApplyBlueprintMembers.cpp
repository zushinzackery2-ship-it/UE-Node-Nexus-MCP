#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "Blueprint/NexusBlueprintPinTypes.h"
#include "NexusBlueprintAssetType.h"

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
    if (!(bVariable || bComponent || bFunction || bLocal || bDefault))
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

void ApplyBlueprintPlan(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    if (Blueprint == nullptr)
    {
        Context.Fail(INDEX_NONE, TEXT("invalid_asset"), TEXT("asset is not a Blueprint"));
        return;
    }
    TArray<UEdGraph*> Touched;
    for (int32 Index = 0; Index < Plan.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Op = Plan[Index].IsValid() ? Plan[Index]->AsObject() : nullptr;
        if (!Op.IsValid())
        {
            Context.Fail(Index, TEXT("invalid_verb"), TEXT("plan entries must be objects"));
            continue;
        }
        const FString Verb = ReadOpString(Op, TEXT("op"));
        if (Verb == TEXT("set_asset_prop"))
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
            continue;
        }
        if (ApplyBlueprintMemberVerb(Blueprint, Verb, Op, Index, Context))
        {
            continue;
        }
        const FString GraphName = ReadOpString(Op, TEXT("graph"), TEXT("EventGraph"));
        UEdGraph* Graph = FindBlueprintGraph(Blueprint, GraphName);
        if (Graph == nullptr)
        {
            Context.Fail(Index, TEXT("graph_not_found"), FString::Printf(TEXT("graph not found: %s"), *GraphName));
            continue;
        }
        Touched.AddUnique(Graph);
        ApplyBlueprintGraphVerb(Blueprint, Graph, Op, Index, Context);
    }
    if (Context.bChanged && !Context.bDryRun)
    {
        SettleGraphPinTypes(Touched, Context);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
}
}
