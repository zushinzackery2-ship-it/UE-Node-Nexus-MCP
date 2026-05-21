#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UEdGraphPin;

namespace UeNodeNexusBridge
{
struct FCompactGraphBuilder
{
    TSharedPtr<FJsonObject> Data;
    TMap<FString, FString> NodeAliases;
    TMap<FString, FString> PinAliases;
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Pins;
    TArray<TSharedPtr<FJsonValue>> Links;
    TArray<TSharedPtr<FJsonValue>> Params;
    int32 NextNodeIndex = 0;
    int32 NextPinIndex = 0;

    FCompactGraphBuilder();

    FString AliasNode(const FString& RealNodeId);
    FString AliasPin(const FString& RealPinId);
    void AddNode(const FString& RealNodeId, const FString& ClassName, const FString& DisplayName, int32 X, int32 Y);
    void AddPin(const FString& RealPinId, const FString& RealNodeId, const FString& Direction, const FString& Name, const FString& Type, const FString& DefaultValue);
    void AddLink(const FString& FromPinId, const FString& ToPinId);
    void AddParam(const FString& RealNodeId, const TSharedPtr<FJsonObject>& Param);
    void FinalizeIds();
};

FString CompactBlueprintPinType(const UEdGraphPin* Pin);
FString CompactParamValue(const TSharedPtr<FJsonObject>& Param);
}
