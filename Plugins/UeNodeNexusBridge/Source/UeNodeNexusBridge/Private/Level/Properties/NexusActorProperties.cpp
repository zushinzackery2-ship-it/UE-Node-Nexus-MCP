#include "NexusActorProperties.h"

#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::ActorProperties
{
bool Resolve(UObject* Target, const FString& Name, FPath& Out, FString& Error)
{
    Out = FPath();
    TArray<FString> Parts;
    Name.ParseIntoArray(Parts, TEXT("."), true);
    if (Parts.IsEmpty())
    {
        Error = TEXT("empty_property_path");
        return false;
    }
    UStruct* Scope = Target->GetClass();
    void* Container = Target;
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        FProperty* Property = Scope->FindPropertyByName(FName(*Parts[Index]));
        if (!Property)
        {
            Error = TEXT("unknown_property:") + Parts[Index];
            return false;
        }
        if (!ShouldExposeProperty(Property, false))
        {
            Error = TEXT("property_not_editable:") + Parts[Index];
            return false;
        }
        if (Property->ArrayDim != 1)
        {
            Error = TEXT("static_array_unsupported:") + Parts[Index];
            return false;
        }
        if (Index == 0)
        {
            Out.Root = Property;
        }
        if (Index == Parts.Num() - 1)
        {
            Out.Leaf = Property;
            Out.Container = Container;
            return true;
        }
        auto* Struct = CastField<FStructProperty>(Property);
        if (!Struct)
        {
            Error = TEXT("not_a_struct:") + Parts[Index];
            return false;
        }
        Container = Struct->ContainerPtrToValuePtr<void>(Container);
        Scope = Struct->Struct;
    }
    Error = TEXT("unresolved_property_path");
    return false;
}

FString Export(UObject* Owner, const FPath& Path)
{
    FString Text;
    const void* Value = Path.Leaf->ContainerPtrToValuePtr<void>(Path.Container);
    Path.Leaf->ExportText_Direct(Text, Value, Value, Owner, PPF_None);
    return Text;
}

bool Write(UObject* Owner, const FPath& Resolved, const TSharedPtr<FJsonValue>& Value, FString& Error)
{
    void* ValuePtr = Resolved.Leaf->ContainerPtrToValuePtr<void>(Resolved.Container);
    if (auto* ObjectProperty = CastField<FObjectPropertyBase>(Resolved.Leaf))
    {
        FString Path;
        if (!Value.IsValid() || !Value->TryGetString(Path))
        {
            Error = TEXT("object_property_value_must_be_path_string");
            return false;
        }
        const bool bNone = Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase);
        UObject* Object = bNone ? nullptr : ResolveObjectByPath(Path);
        if (!bNone && !Object)
        {
            Error = TEXT("object_property_value_not_found");
            return false;
        }
        if (Object && ObjectProperty->PropertyClass && !Object->IsA(ObjectProperty->PropertyClass))
        {
            Error = TEXT("object_property_class_mismatch");
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(ValuePtr, Object);
        return true;
    }
    FString Text;
    if ((!Value.IsValid() || !Value->TryGetString(Text))
        && !JsonValueToPropertyImportText(Resolved.Leaf, Value, Text, Error))
    {
        return false;
    }
    if (!Resolved.Leaf->ImportText_Direct(*Text, ValuePtr, Owner, PPF_None))
    {
        Error = TEXT("import_failed:") + Text;
        return false;
    }
    return true;
}
}
