#include "NexusSceneProperties.h"

#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Scene
{
static void GatherValue(FProperty* Property, const void* Value, TSet<UObject*>& References)
{
    if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
    {
        if (UObject* Referenced = Object->GetObjectPropertyValue(Value))
        {
            References.Add(Referenced);
        }
    }
    else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Values(Array, Value);
        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            GatherValue(Array->Inner, Values.GetRawPtr(Index), References);
        }
    }
    else if (FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
        {
            GatherValue(*It, It->ContainerPtrToValuePtr<void>(Value), References);
        }
    }
    else if (FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Values(Set, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                GatherValue(Set->ElementProp, Values.GetElementPtr(Index), References);
            }
        }
    }
    else if (FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Values(Map, Value);
        for (int32 Index = 0; Index < Values.GetMaxIndex(); ++Index)
        {
            if (Values.IsValidIndex(Index))
            {
                GatherValue(Map->KeyProp, Values.GetKeyPtr(Index), References);
                GatherValue(Map->ValueProp, Values.GetValuePtr(Index), References);
            }
        }
    }
}

bool GatherPropertyReferences(UObject* Target, const FObject& Properties, TSet<UObject*>& References, FString& Error)
{
    for (const auto& Pair : Properties->Values)
    {
        FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*Pair.Key));
        if (!EditableProperty(Property))
        {
            Error = TEXT("property_not_editable: ") + Pair.Key;
            return false;
        }
        void* Temporary = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Temporary);
        ON_SCOPE_EXIT
        {
            Property->DestroyValue(Temporary);
            FMemory::Free(Temporary);
        };
        const FString Text = Pair.Value->AsString();
        const TCHAR* End = Property->ImportText_Direct(*Text, Temporary, Target, PPF_None);
        if (!End || !FString(End).TrimStartAndEnd().IsEmpty())
        {
            Error = TEXT("invalid_resource_reference: ") + Pair.Key;
            return false;
        }
        GatherValue(Property, Temporary, References);
    }
    return true;
}
}
