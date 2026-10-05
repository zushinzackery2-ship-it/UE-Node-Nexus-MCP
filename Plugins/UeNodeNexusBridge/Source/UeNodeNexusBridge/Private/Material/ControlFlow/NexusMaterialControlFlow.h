#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class UClass;
class UMaterialExpression;
class UObject;

namespace UeNodeNexusBridge::Transcode
{
void AppendMaterialControlFlowPins(UMaterialExpression* Expression,
    TArray<TSharedPtr<FJsonValue>>& Inputs, TArray<TSharedPtr<FJsonValue>>& Outputs);
void AppendMaterialControlFlowLinks(UMaterialExpression* Expression,
    TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, TArray<TSharedPtr<FJsonValue>>& Links);
bool ApplyMaterialControlFlowLink(UObject* Owner, UMaterialExpression* From,
    UMaterialExpression* To, const FString& FromPin, bool bConnect, FString& OutError);
void BindMaterialControlFlowRoot(UObject* Owner, UMaterialExpression* Expression);
UMaterialExpression* FindMaterialControlFlowRoot(UObject* Owner, UClass* Class);
void DisconnectMaterialControlFlowExpression(UObject* Owner, UMaterialExpression* Expression);
}
