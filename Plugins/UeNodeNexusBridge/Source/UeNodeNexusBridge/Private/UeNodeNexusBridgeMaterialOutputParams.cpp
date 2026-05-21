#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool IsEditableMaterialOutputProperty(FProperty* Property)
{
    if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
    {
        return false;
    }
    return Property->IsA<FBoolProperty>()
        || Property->IsA<FByteProperty>()
        || Property->IsA<FEnumProperty>()
        || Property->IsA<FNumericProperty>()
        || Property->IsA<FNameProperty>()
        || Property->IsA<FStrProperty>()
        || Property->IsA<FTextProperty>();
}

static TArray<TSharedPtr<FJsonValue>> BuildChoices(FProperty* Property)
{
    TArray<TSharedPtr<FJsonValue>> Choices;
    if (Property->IsA<FBoolProperty>())
    {
        Choices.Add(MakeShared<FJsonValueString>(TEXT("False")));
        Choices.Add(MakeShared<FJsonValueString>(TEXT("True")));
        return Choices;
    }

    UEnum* Enum = nullptr;
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        Enum = EnumProperty->GetEnum();
    }
    else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        Enum = ByteProperty->Enum;
    }
    if (Enum == nullptr)
    {
        return Choices;
    }

    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        if (!Enum->HasMetaData(TEXT("Hidden"), Index))
        {
            Choices.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
        }
    }
    return Choices;
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialOutputParams(UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Material == nullptr)
    {
        return Params;
    }

    int32 Index = 0;
    for (TFieldIterator<FProperty> It(Material->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsEditableMaterialOutputProperty(Property))
        {
            continue;
        }

        FString Value;
        Property->ExportTextItem_InContainer(Value, Material, nullptr, Material, PPF_None);
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("index"), Index++);
        Json->SetStringField(TEXT("name"), Property->GetName());
        Json->SetStringField(TEXT("type"), Property->GetCPPType());
        Json->SetStringField(TEXT("value"), Value);
        Json->SetBoolField(TEXT("editable"), true);

        TArray<TSharedPtr<FJsonValue>> Choices = BuildChoices(Property);
        if (Choices.Num() > 0)
        {
            Json->SetArrayField(TEXT("choices"), Choices);
        }
        Params.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Params;
}

bool ApplyMaterialOutputParamValue(UMaterial* Material, const FString& Name, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    if (Material == nullptr)
    {
        return false;
    }

    FProperty* Property = Material->GetClass()->FindPropertyByName(FName(*Name));
    if (!IsEditableMaterialOutputProperty(Property))
    {
        return false;
    }

    FString OldValue;
    Property->ExportTextItem_InContainer(OldValue, Material, nullptr, Material, PPF_None);
    AddMaterialParamChange(Diff, MaterialOutputNodeId(), Name, OldValue, Value);
    return bDryRun || Property->ImportText_InContainer(*Value, Material, Material, PPF_None) != nullptr;
}
}
