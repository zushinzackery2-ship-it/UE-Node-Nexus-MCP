#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialExpression.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
template <typename TAsset>
bool MatchesMaterialNodeKeyword(TAsset* Asset, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Payload)
{
    FString Keyword;
    if (!Payload->TryGetStringField(TEXT("keyword"), Keyword) || Keyword.IsEmpty())
    {
        return true;
    }
    return Expression->GetName().Contains(Keyword, ESearchCase::IgnoreCase)
        || MaterialNodeAlias(Asset, Expression).Contains(Keyword, ESearchCase::IgnoreCase)
        || MaterialExpressionNodeId(Expression).Contains(Keyword, ESearchCase::IgnoreCase)
        || ShortMaterialExpressionClass(Expression).Contains(Keyword, ESearchCase::IgnoreCase);
}

inline bool MatchesMaterialOutputKeyword(const TSharedPtr<FJsonObject>& Payload)
{
    FString Keyword;
    return !Payload->TryGetStringField(TEXT("keyword"), Keyword) || Keyword.IsEmpty()
        || FString(TEXT("MaterialOutput")).Contains(Keyword, ESearchCase::IgnoreCase);
}

template <typename TAsset>
TArray<UMaterialExpression*> FilterMaterialNodes(
    TAsset* Asset,
    TConstArrayView<TObjectPtr<UMaterialExpression>> Candidates,
    int32 MaxNodes,
    const TSharedPtr<FJsonObject>& Payload,
    int32& OutMatched)
{
    OutMatched = 0;
    TArray<UMaterialExpression*> Selected;
    Selected.Reserve(MaxNodes > 0 ? FMath::Min(MaxNodes, Candidates.Num()) : Candidates.Num());
    for (TObjectPtr<UMaterialExpression> Candidate : Candidates)
    {
        UMaterialExpression* Expression = Candidate.Get();
        if (Expression == nullptr || !MatchesMaterialNodeKeyword(Asset, Expression, Payload))
        {
            continue;
        }
        ++OutMatched;
        if (MaxNodes == 0 || Selected.Num() < MaxNodes)
        {
            Selected.Add(Expression);
        }
    }
    return Selected;
}
}
