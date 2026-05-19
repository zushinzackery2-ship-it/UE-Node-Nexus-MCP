#include "UeNodeNexusBridgeWireGraph.h"

namespace UeNodeNexusBridge
{
FWireGraphBuilder::FWireGraphBuilder(const FString& InTitle)
    : Title(InTitle)
{
}

FString FWireGraphBuilder::AliasNode(const FString& RealNodeId)
{
    if (const FString* Existing = NodeAliases.Find(RealNodeId))
    {
        return *Existing;
    }

    const FString Alias = FString::Printf(TEXT("N%d"), NextNodeIndex++);
    NodeAliases.Add(RealNodeId, Alias);
    return Alias;
}

void FWireGraphBuilder::AddNodeType(const FString& TypeName)
{
    TypeCounts.FindOrAdd(TypeName)++;
}

void FWireGraphBuilder::AddWire(const FString& FromNode, const FString& FromPin, const FString& FromNodeId, const FString& ToNode, const FString& ToPin, const FString& ToNodeId)
{
    const FString FromAlias = AliasNode(FromNodeId);
    const FString ToAlias = AliasNode(ToNodeId);
    NodeLabels.FindOrAdd(FromAlias, FromNode);
    NodeLabels.FindOrAdd(ToAlias, ToNode);
    Lines.Add(FString::Printf(TEXT("  %s.%s (%s) -> %s.%s (%s)"), *FromNode, *FromPin, *FromAlias, *ToNode, *ToPin, *ToAlias));
    MinEdges.Add(FString::Printf(TEXT("%s.%s>%s.%s"), *FromAlias, *FromPin, *ToAlias, *ToPin));
    TinyEdges.Add(FString::Printf(TEXT("%s.%s>%s.%s"), *StripWireGraphNodePrefix(FromAlias), *ShortWireGraphPinName(FromPin), *StripWireGraphNodePrefix(ToAlias), *ShortWireGraphPinName(ToPin)));
}

FString FWireGraphBuilder::BuildText() const
{
    FString Text;
    Text += TEXT("========================================================================\n");
    Text += FString::Printf(TEXT("  %s - %d wires\n"), *Title, Lines.Num());
    Text += TEXT("========================================================================\n");

    for (const FString& Line : Lines)
    {
        Text += Line;
        Text += TEXT("\n");
    }

    Text += TEXT("\n------------------------------------------------------------------------\n");
    Text += TEXT("  Node type stats:\n");

    TArray<TPair<FString, int32>> Counts;
    for (const TPair<FString, int32>& Pair : TypeCounts)
    {
        Counts.Add(Pair);
    }
    Counts.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
    {
        if (A.Value == B.Value)
        {
            return A.Key < B.Key;
        }
        return A.Value > B.Value;
    });

    for (const TPair<FString, int32>& Pair : Counts)
    {
        Text += FString::Printf(TEXT("    %s x %d\n"), *Pair.Key, Pair.Value);
    }

    return Text;
}

FString FWireGraphBuilder::BuildTinyText() const
{
    FString Text = FString::Printf(TEXT("W%d\nN:"), TinyEdges.Num());

    TArray<TPair<FString, FString>> Labels;
    for (const TPair<FString, FString>& Pair : NodeLabels)
    {
        Labels.Add(Pair);
    }
    Labels.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
    {
        return FCString::Atoi(*StripWireGraphNodePrefix(A.Key)) < FCString::Atoi(*StripWireGraphNodePrefix(B.Key));
    });

    for (int32 Index = 0; Index < Labels.Num(); ++Index)
    {
        if (Index > 0)
        {
            Text += TEXT(";");
        }
        Text += StripWireGraphNodePrefix(Labels[Index].Key);
        Text += TEXT("=");
        Text += ShortWireGraphNodeLabel(Labels[Index].Value);
    }

    Text += TEXT("\nE:");
    for (int32 Index = 0; Index < TinyEdges.Num(); ++Index)
    {
        if (Index > 0)
        {
            Text += TEXT(";");
        }
        Text += TinyEdges[Index];
    }

    Text += TEXT("\nT:");
    TArray<TPair<FString, int32>> Counts;
    for (const TPair<FString, int32>& Pair : TypeCounts)
    {
        Counts.Add(Pair);
    }
    Counts.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
    {
        return A.Value == B.Value ? A.Key < B.Key : A.Value > B.Value;
    });
    for (int32 Index = 0; Index < Counts.Num(); ++Index)
    {
        if (Index > 0)
        {
            Text += TEXT(",");
        }
        Text += ShortWireGraphTypeName(Counts[Index].Key);
        Text += FString::FromInt(Counts[Index].Value);
    }
    Text += TEXT("\n");
    return Text;
}

FString FWireGraphBuilder::BuildMinText() const
{
    FString Text;
    Text += FString::Printf(TEXT("W=%d\n"), MinEdges.Num());
    Text += TEXT("N:\n");

    TArray<TPair<FString, FString>> Labels;
    for (const TPair<FString, FString>& Pair : NodeLabels)
    {
        Labels.Add(Pair);
    }
    Labels.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
    {
        return FCString::Atoi(*A.Key.Mid(1)) < FCString::Atoi(*B.Key.Mid(1));
    });

    for (const TPair<FString, FString>& Pair : Labels)
    {
        Text += Pair.Key;
        Text += TEXT("=");
        Text += Pair.Value;
        Text += TEXT("\n");
    }

    Text += TEXT("E:\n");
    for (const FString& Edge : MinEdges)
    {
        Text += Edge;
        Text += TEXT("\n");
    }

    Text += TEXT("T:");
    TArray<TPair<FString, int32>> Counts;
    for (const TPair<FString, int32>& Pair : TypeCounts)
    {
        Counts.Add(Pair);
    }
    Counts.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
    {
        if (A.Value == B.Value)
        {
            return A.Key < B.Key;
        }
        return A.Value > B.Value;
    });

    for (int32 Index = 0; Index < Counts.Num(); ++Index)
    {
        if (Index > 0)
        {
            Text += TEXT(",");
        }
        Text += FString::Printf(TEXT("%s=%d"), *Counts[Index].Key, Counts[Index].Value);
    }
    Text += TEXT("\n");
    return Text;
}

FString MakeWireGraphNodeLabel(const FString& ClassName, const FString& DisplayName)
{
    FString TypeName = ClassName;
    TypeName.RemoveFromStart(TEXT("MaterialExpression"));
    TypeName.RemoveFromStart(TEXT("K2Node_"));
    TypeName.RemoveFromStart(TEXT("EdGraphNode_"));

    if (!DisplayName.IsEmpty() && !DisplayName.StartsWith(TypeName))
    {
        return FString::Printf(TEXT("%s(%s)"), *TypeName, *DisplayName);
    }
    return TypeName;
}

TSharedPtr<FJsonObject> MakeWireGraphData(const FString& AssetPath, const FString& AssetClass, const FString& GraphName, const FString& GraphKind, const FString& Text)
{
    return MakeWireGraphData(AssetPath, AssetClass, GraphName, GraphKind, Text, TEXT("wires_text_v1"));
}

TSharedPtr<FJsonObject> MakeWireGraphData(const FString& AssetPath, const FString& AssetClass, const FString& GraphName, const FString& GraphKind, const FString& Text, const FString& Format)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format);
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_class"), AssetClass);
    Data->SetStringField(TEXT("graph_name"), GraphName);
    Data->SetStringField(TEXT("graph_kind"), GraphKind);
    Data->SetStringField(TEXT("text"), Text);
    return Data;
}
}
