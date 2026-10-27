#include "Material/Functions/NexusFunctionCallRebind.h"

#include "EdGraph/EdGraphNode.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Misc/PackageName.h"
#include "SceneTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusFunctionCallRebind, Log, All);

namespace UeNodeNexusBridge
{
namespace
{
struct FCallReaders
{
    UMaterialExpressionMaterialFunctionCall* Call = nullptr;
    const TArray<FFunctionExpressionOutput>* Before = nullptr;
    FFunctionCallRebind* Result = nullptr;
    TSet<const FExpressionInput*> Visited;

    // Points Input at the new index of the output it read; the lost output name when the new function lacks it.
    FName Follow(FExpressionInput* Input, UObject* Reader)
    {
        if (Input == nullptr || Input->Expression != Call || Visited.Contains(Input))
        {
            return NAME_None;
        }
        Visited.Add(Input);
        const FName Name = Before->IsValidIndex(Input->OutputIndex) ? (*Before)[Input->OutputIndex].Output.OutputName : NAME_None;
        const int32 Index = Name.IsNone() ? INDEX_NONE : Call->FunctionOutputs.IndexOfByPredicate(
            [Name](const FFunctionExpressionOutput& Output)
            {
                return Output.Output.OutputName == Name;
            });
        Reader->Modify();
        if (Index == INDEX_NONE)
        {
            Input->Expression = nullptr;
            Input->OutputIndex = 0;
            return Name.IsNone() ? FName(TEXT("<invalid output index>")) : Name;
        }
        Input->Connect(Index, Call);
        ++Result->Followed;
        return NAME_None;
    }
};

void FollowOwnerReaders(FCallReaders& Readers)
{
    UObject* Owner = Readers.Call->GetOuter();
    UMaterial* Material = Cast<UMaterial>(Owner);
    UMaterialFunction* Function = Cast<UMaterialFunction>(Owner);
    TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions;
    if (Material != nullptr)
    {
        Expressions = Material->GetExpressions();
    }
    else if (Function != nullptr)
    {
        Expressions = Function->GetExpressions();
    }
    for (const TObjectPtr<UMaterialExpression>& Reader : Expressions)
    {
        if (Reader == nullptr)
        {
            continue;
        }
        for (FExpressionInputIterator It{ Reader.Get() }; It; ++It)
        {
            const FName Lost = Readers.Follow(It.Input, Reader.Get());
            if (!Lost.IsNone())
            {
                Readers.Result->Dropped.Add(FString::Printf(TEXT("%s.%s <- %s"),
                    *Reader->GetName(), *Reader->GetInputName(It.Index).ToString(), *Lost.ToString()));
            }
        }
    }
    if (Material == nullptr)
    {
        return;
    }
    const UEnum* Properties = StaticEnum<EMaterialProperty>();
    for (int32 Property = 0; Property < MP_MAX; ++Property)
    {
        const FName Lost = Readers.Follow(Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Property)), Material);
        if (!Lost.IsNone())
        {
            Readers.Result->Dropped.Add(FString::Printf(TEXT("out.%s <- %s"),
                *Properties->GetNameStringByValue(Property).RightChop(3), *Lost.ToString()));
        }
    }
}
}

bool ResolveCalledFunction(const FString& Text, UMaterialFunctionInterface*& OutFunction, FString& OutError)
{
    OutFunction = nullptr;
    const FString Path = FPackageName::ExportTextPathToObjectPath(Text.TrimStartAndEnd());
    if (Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    OutFunction = LoadObject<UMaterialFunctionInterface>(nullptr, *Path);
    if (OutFunction == nullptr)
    {
        OutError = FString::Printf(TEXT("material function not found: %s"), *Path);
        return false;
    }
    return true;
}

bool RebindFunctionCall(UMaterialExpressionMaterialFunctionCall* Call, UMaterialFunctionInterface* Function,
    FFunctionCallRebind& OutResult, FString& OutError)
{
    UMaterialFunction* OwnerFunction = Cast<UMaterialFunction>(Call->GetOuter());
    if (Function != nullptr && OwnerFunction != nullptr && Function->IsDependent(OwnerFunction))
    {
        OutError = FString::Printf(TEXT("%s cannot call %s: the call would be circular"), *OwnerFunction->GetPathName(), *Function->GetPathName());
        UE_LOG(LogNexusFunctionCallRebind, Warning, TEXT("rebind refused: %s"), *OutError);
        return false;
    }
    const FString Previous = Call->MaterialFunction ? Call->MaterialFunction->GetPathName() : FString(TEXT("None"));
    const TArray<FFunctionExpressionOutput> Before = Call->FunctionOutputs;
    Call->Modify();
    if (Function == nullptr)
    {
        Call->MaterialFunction = nullptr;
        Call->FunctionInputs.Reset();
        Call->FunctionOutputs.Reset();
        Call->Outputs.Reset();
    }
    else
    {
        TGuardValue<TObjectPtr<UEdGraphNode>> NodeGuard(Call->GraphNode, nullptr);
        TGuardValue<TObjectPtr<UMaterial>> MaterialGuard(Call->Material, nullptr);
        TGuardValue<TObjectPtr<UMaterialFunction>> FunctionGuard(Call->Function, nullptr);
        if (!Call->SetMaterialFunction(Function))
        {
            OutError = FString::Printf(TEXT("%s refused material function %s"), *Call->GetName(), *Function->GetPathName());
            UE_LOG(LogNexusFunctionCallRebind, Warning, TEXT("rebind refused: %s"), *OutError);
            return false;
        }
    }
    FCallReaders Readers;
    Readers.Call = Call;
    Readers.Before = &Before;
    Readers.Result = &OutResult;
    FollowOwnerReaders(Readers);
    if (UMaterialGraphNode* Node = Cast<UMaterialGraphNode>(Call->GraphNode))
    {
        // Pins are relinked from the expression links, so only once the readers follow.
        Node->RecreateAndLinkNode();
    }
    UE_LOG(LogNexusFunctionCallRebind, Log, TEXT("%s in %s: %s -> %s, followed %d link(s), dropped %d"),
        *Call->GetName(), *GetNameSafe(Call->GetOuter()), *Previous,
        Function ? *Function->GetPathName() : TEXT("None"), OutResult.Followed, OutResult.Dropped.Num());
    for (const FString& Dropped : OutResult.Dropped)
    {
        UE_LOG(LogNexusFunctionCallRebind, Warning, TEXT("%s: new function has no output for %s"), *Call->GetName(), *Dropped);
    }
    return true;
}
}
