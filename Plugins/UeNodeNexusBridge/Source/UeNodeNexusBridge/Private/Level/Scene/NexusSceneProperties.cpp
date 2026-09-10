#include "NexusSceneProperties.h"

#include "Misc/ScopeExit.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Scene
{
bool EditableProperty(const FProperty* Property)
{
    static const TSet<FName> Reserved =
    {
        TEXT("PerInstanceSMData"), TEXT("PerInstanceSMCustomData"), TEXT("NumCustomDataFloats"),
        TEXT("RelativeLocation"), TEXT("RelativeRotation"), TEXT("RelativeScale3D"),
        TEXT("ActorLabel"), TEXT("FolderPath"), TEXT("AssetUserData"), TEXT("AssetUserDataEditorOnly")
    };
    return Transcode::IsEditableProperty(Property) && Property->ArrayDim == 1
        && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_DisableEditOnInstance)
        && !Reserved.Contains(Property->GetFName());
}

void ExportProperties(UObject* Target, FObject& Properties, FObject& Defaults, FObject& Schema)
{
    Properties = MakeShared<FJsonObject>();
    Defaults = MakeShared<FJsonObject>();
    Schema = MakeShared<FJsonObject>();
    UObject* Archetype = Target->GetArchetype();
    for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!EditableProperty(Property))
        {
            continue;
        }
        const FString Name = Property->GetName();
        const FString Value = Transcode::ExportPropertyValue(Target, Property);
        const FString Default = Transcode::ExportPropertyValue(Archetype, Property);
        if (Value != Default)
        {
            Properties->SetStringField(Name, Value);
        }
        Defaults->SetStringField(Name, Default);
        Schema->SetObjectField(Name, Transcode::PropertySchemaJson(Property, Archetype));
    }
}

static bool ValidateValue(UObject* Target, FProperty* Property, const FString& Value, FString& Error)
{
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
        ObjectProperty && !CastField<FSoftObjectProperty>(Property))
    {
        const bool bNull = Value.Equals(TEXT("None"), ESearchCase::IgnoreCase);
        UObject* ObjectValue = bNull ? nullptr : ResolveObjectByPath(FPackageName::ExportTextPathToObjectPath(Value));
        if ((!bNull && !ObjectValue) || (ObjectValue && !ObjectValue->IsA(ObjectProperty->PropertyClass)))
        {
            Error = TEXT("invalid_object_reference: ") + Property->GetName();
            return false;
        }
        return true;
    }
    void* Temporary = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Temporary);
    ON_SCOPE_EXIT
    {
        Property->DestroyValue(Temporary);
        FMemory::Free(Temporary);
    };
    const TCHAR* End = Property->ImportText_Direct(*Value, Temporary, Target, PPF_None);
    if (!End || !FString(End).TrimStartAndEnd().IsEmpty())
    {
        Error = TEXT("invalid_property_value: ") + Property->GetName();
        return false;
    }
    return true;
}

bool WriteProperties(UObject* Target, const FObject& Properties, bool bApply, FString& Error, bool bNotify)
{
    for (const auto& Pair : Properties->Values)
    {
        FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*Pair.Key));
        FString Value;
        if (!EditableProperty(Property) || !Pair.Value->TryGetString(Value))
        {
            Error = TEXT("property_not_editable: ") + Pair.Key;
            return false;
        }
        if (!ValidateValue(Target, Property, Value, Error))
        {
            return false;
        }
    }
    if (bApply && !Properties->Values.IsEmpty())
    {
        Target->Modify();
        Target->PreEditChange(nullptr);
        for (const auto& Pair : Properties->Values)
        {
            if (!Transcode::ImportPropertyValue(Target, Pair.Key, Pair.Value->AsString(), Error, false))
            {
                Target->PostEditChange();
                return false;
            }
        }
        if (bNotify)
        {
            Target->PostEditChange();
        }
        Target->MarkPackageDirty();
    }
    return true;
}
}
