#include "NexusBlueprintNodeProperties.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UeNodeNexusBridgeObjectPropertyJsonValidation.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusPropertyValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
namespace
{
bool ValidateNodeValue(const FProperty* Property, const TSharedPtr<FJsonValue>& Value,
    const FString& Path, FString& Error)
{
    if (!Property || !Value.IsValid())
    {
        Error = TEXT("unknown_node_property: ") + Path;
        return false;
    }
    if (!Transcode::IsEditableProperty(Property))
    {
        Error = TEXT("node_property_not_editable: ") + Path;
        return false;
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property);
        Struct && Value->Type == EJson::Object)
    {
        if (!ValidateStructJson(Struct->Struct, Value->AsObject(), Path + TEXT("."), Error))
        {
            return false;
        }
        for (const auto& Pair : Value->AsObject()->Values)
        {
            if (!ValidateNodeValue(ResolveStructJsonField(Struct->Struct, Pair.Key), Pair.Value,
                Path + TEXT(".") + Pair.Key, Error))
            {
                return false;
            }
        }
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        FString ObjectPath;
        if (Value->Type != EJson::Null && !Value->TryGetString(ObjectPath))
        {
            Error = TEXT("object_property_value_must_be_path_string: ") + Path;
            return false;
        }
        if (ObjectPath.IsEmpty() || ObjectPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            return true;
        }
        UObject* Object = ResolveObjectByPath(ObjectPath);
        if (!Object || !Object->IsA(ObjectProperty->PropertyClass))
        {
            Error = TEXT("object_property_missing_or_class_mismatch: ") + Path;
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonValue> ExpandPath(const TArray<FString>& Parts, const TSharedPtr<FJsonValue>& Value)
{
    TSharedPtr<FJsonValue> Expanded = Value;
    for (int32 Index = Parts.Num() - 1; Index > 0; --Index)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetField(Parts[Index], Expanded);
        Expanded = MakeShared<FJsonValueObject>(Object);
    }
    return Expanded;
}
}

bool ApplyBlueprintNodeProperty(UEdGraphNode* Node, const FString& Name,
    const TSharedPtr<FJsonValue>& Value, bool bDryRun, bool bNotify,
    FString& Before, FString& After, FString& Error)
{
    TArray<FString> Parts;
    Name.ParseIntoArray(Parts, TEXT("."), false);
    if (!Node || Parts.IsEmpty() || Parts.Contains(FString()))
    {
        Error = TEXT("invalid_node_property_path: ") + Name;
        return false;
    }
    FProperty* Property = Node->GetClass()->FindPropertyByName(FName(*Parts[0]));
    const TSharedPtr<FJsonValue> Expanded = ExpandPath(Parts, Value);
    if (!ValidateNodeValue(Property, Expanded, Parts[0], Error))
    {
        return false;
    }
    FPropertyValueBuffer Buffer(Node, Property);
    if (!Buffer.ApplyJson(Expanded, After, Error))
    {
        return false;
    }
    Before = Transcode::ExportPropertyValue(Node, Property);
    if (!bDryRun && Buffer.Changed())
    {
        Node->Modify();
        Buffer.Commit();
        if (bNotify)
        {
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
            Node->PostEditChangeProperty(Event);
            Node->ReconstructNode();
        }
    }
    return true;
}

bool ConfigureBlueprintNodeProperties(UEdGraphNode* Node,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& Error)
{
    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (!Payload->TryGetObjectField(TEXT("params"), Params))
    {
        return true;
    }
    for (int32 Pass = 0; Pass < (bDryRun ? 1 : 2); ++Pass)
    {
        for (const auto& Pair : (*Params)->Values)
        {
            FString Before;
            FString After;
            if (!ApplyBlueprintNodeProperty(Node, Pair.Key, Pair.Value, Pass == 0, false,
                Before, After, Error))
            {
                return false;
            }
        }
    }
    return true;
}
}
