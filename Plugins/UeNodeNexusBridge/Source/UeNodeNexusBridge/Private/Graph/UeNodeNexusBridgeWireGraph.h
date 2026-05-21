#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
struct FWireGraphBuilder
{
    FString Title;
    TMap<FString, FString> NodeAliases;
    TMap<FString, FString> NodeLabels;
    TMap<FString, int32> TypeCounts;
    TArray<FString> Lines;
    TArray<FString> MinEdges;
    TArray<FString> TinyEdges;
    int32 NextNodeIndex = 0;

    explicit FWireGraphBuilder(const FString& InTitle);

    FString AliasNode(const FString& RealNodeId);
    void AddNodeType(const FString& TypeName);
    void AddWire(const FString& FromNode, const FString& FromPin, const FString& FromNodeId, const FString& ToNode, const FString& ToPin, const FString& ToNodeId);
    FString BuildText() const;
    FString BuildMinText() const;
    FString BuildTinyText() const;
};

FString MakeWireGraphNodeLabel(const FString& ClassName, const FString& DisplayName);
FString ShortWireGraphTypeName(FString TypeName);
FString ShortWireGraphPinName(FString PinName);
FString ShortWireGraphNodeLabel(const FString& Label);
FString StripWireGraphNodePrefix(const FString& Alias);
TSharedPtr<FJsonObject> MakeWireGraphData(const FString& AssetPath, const FString& AssetClass, const FString& GraphName, const FString& GraphKind, const FString& Text);
TSharedPtr<FJsonObject> MakeWireGraphData(const FString& AssetPath, const FString& AssetClass, const FString& GraphName, const FString& GraphKind, const FString& Text, const FString& Format);
}
