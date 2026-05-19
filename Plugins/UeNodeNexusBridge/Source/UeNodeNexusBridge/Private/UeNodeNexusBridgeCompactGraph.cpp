#include "UeNodeNexusBridgeCompactGraph.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeCompactColumns()
{
    TSharedPtr<FJsonObject> Columns = MakeShared<FJsonObject>();
    Columns->SetArrayField(TEXT("nodes"), {
        MakeShared<FJsonValueString>(TEXT("id")),
        MakeShared<FJsonValueString>(TEXT("class")),
        MakeShared<FJsonValueString>(TEXT("name")),
        MakeShared<FJsonValueString>(TEXT("x")),
        MakeShared<FJsonValueString>(TEXT("y"))
    });
    Columns->SetArrayField(TEXT("pins"), {
        MakeShared<FJsonValueString>(TEXT("id")),
        MakeShared<FJsonValueString>(TEXT("node")),
        MakeShared<FJsonValueString>(TEXT("dir")),
        MakeShared<FJsonValueString>(TEXT("name")),
        MakeShared<FJsonValueString>(TEXT("type")),
        MakeShared<FJsonValueString>(TEXT("default"))
    });
    Columns->SetArrayField(TEXT("links"), {
        MakeShared<FJsonValueString>(TEXT("from_pin")),
        MakeShared<FJsonValueString>(TEXT("to_pin"))
    });
    Columns->SetArrayField(TEXT("params"), {
        MakeShared<FJsonValueString>(TEXT("node")),
        MakeShared<FJsonValueString>(TEXT("name")),
        MakeShared<FJsonValueString>(TEXT("type")),
        MakeShared<FJsonValueString>(TEXT("value")),
        MakeShared<FJsonValueString>(TEXT("editable"))
    });
    return Columns;
}

FCompactGraphBuilder::FCompactGraphBuilder()
{
    Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("compact_graph"));
    Data->SetObjectField(TEXT("columns"), MakeCompactColumns());
}

FString FCompactGraphBuilder::AliasNode(const FString& RealNodeId)
{
    if (const FString* Existing = NodeAliases.Find(RealNodeId))
    {
        return *Existing;
    }

    const FString Alias = FString::Printf(TEXT("n%d"), NextNodeIndex++);
    NodeAliases.Add(RealNodeId, Alias);
    return Alias;
}

FString FCompactGraphBuilder::AliasPin(const FString& RealPinId)
{
    if (const FString* Existing = PinAliases.Find(RealPinId))
    {
        return *Existing;
    }

    const FString Alias = FString::Printf(TEXT("p%d"), NextPinIndex++);
    PinAliases.Add(RealPinId, Alias);
    return Alias;
}

void FCompactGraphBuilder::AddNode(const FString& RealNodeId, const FString& ClassName, const FString& DisplayName, int32 X, int32 Y)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AliasNode(RealNodeId)));
    Row.Add(MakeShared<FJsonValueString>(ClassName));
    Row.Add(MakeShared<FJsonValueString>(DisplayName));
    Row.Add(MakeShared<FJsonValueNumber>(X));
    Row.Add(MakeShared<FJsonValueNumber>(Y));
    Nodes.Add(MakeShared<FJsonValueArray>(Row));
}

void FCompactGraphBuilder::AddPin(const FString& RealPinId, const FString& RealNodeId, const FString& Direction, const FString& Name, const FString& Type, const FString& DefaultValue)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AliasPin(RealPinId)));
    Row.Add(MakeShared<FJsonValueString>(AliasNode(RealNodeId)));
    Row.Add(MakeShared<FJsonValueString>(Direction));
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Type));
    Row.Add(MakeShared<FJsonValueString>(DefaultValue));
    Pins.Add(MakeShared<FJsonValueArray>(Row));
}

void FCompactGraphBuilder::AddLink(const FString& FromPinId, const FString& ToPinId)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AliasPin(FromPinId)));
    Row.Add(MakeShared<FJsonValueString>(AliasPin(ToPinId)));
    Links.Add(MakeShared<FJsonValueArray>(Row));
}

void FCompactGraphBuilder::AddParam(const FString& RealNodeId, const TSharedPtr<FJsonObject>& Param)
{
    if (!Param.IsValid())
    {
        return;
    }

    FString Name;
    FString Type;
    bool bEditable = false;
    Param->TryGetStringField(TEXT("name"), Name);
    Param->TryGetStringField(TEXT("type"), Type);
    Param->TryGetBoolField(TEXT("editable"), bEditable);

    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AliasNode(RealNodeId)));
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Type));
    Row.Add(MakeShared<FJsonValueString>(CompactParamValue(Param)));
    Row.Add(MakeShared<FJsonValueBoolean>(bEditable));
    Params.Add(MakeShared<FJsonValueArray>(Row));
}

void FCompactGraphBuilder::FinalizeIds()
{
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("pins"), Pins);
    Data->SetArrayField(TEXT("links"), Links);
    Data->SetArrayField(TEXT("params"), Params);

    TSharedPtr<FJsonObject> Ids = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> NodeIdMap = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> PinIdMap = MakeShared<FJsonObject>();

    for (const TPair<FString, FString>& Pair : NodeAliases)
    {
        NodeIdMap->SetStringField(Pair.Value, Pair.Key);
    }
    for (const TPair<FString, FString>& Pair : PinAliases)
    {
        PinIdMap->SetStringField(Pair.Value, Pair.Key);
    }

    Ids->SetObjectField(TEXT("nodes"), NodeIdMap);
    Ids->SetObjectField(TEXT("pins"), PinIdMap);
    Data->SetObjectField(TEXT("ids"), Ids);
}

FString CompactBlueprintPinType(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return FString();
    }

    FString Result = Pin->PinType.PinCategory.ToString();
    const FString Subcategory = Pin->PinType.PinSubCategory.ToString();
    if (!Subcategory.IsEmpty())
    {
        Result += TEXT(":");
        Result += Subcategory;
    }
    if (Pin->PinType.PinSubCategoryObject.IsValid())
    {
        Result += TEXT(":");
        Result += Pin->PinType.PinSubCategoryObject->GetName();
    }
    return Result;
}

FString CompactParamValue(const TSharedPtr<FJsonObject>& Param)
{
    FString Value;
    if (!Param.IsValid())
    {
        return Value;
    }
    if (Param->TryGetStringField(TEXT("value"), Value))
    {
        return Value;
    }
    Param->TryGetStringField(TEXT("default_value"), Value);
    return Value;
}
}
