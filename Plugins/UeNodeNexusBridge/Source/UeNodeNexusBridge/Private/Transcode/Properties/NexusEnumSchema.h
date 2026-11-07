#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
inline TArray<TSharedPtr<FJsonValue>> EnumChoices(UEnum* Enum)
{
    TArray<TSharedPtr<FJsonValue>> Choices;
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        const FString Name = Enum->GetNameStringByIndex(Index);
        if (Name.EndsWith(TEXT("_MAX")))
        {
            continue;
        }
        auto Choice = MakeShared<FJsonObject>();
        Choice->SetStringField(TEXT("name"), Name);
        Choice->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index));
        Choice->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString());
        Choice->SetStringField(TEXT("tooltip"), Enum->GetToolTipTextByIndex(Index).ToString());
        Choice->SetBoolField(TEXT("hidden"), Enum->HasMetaData(TEXT("Hidden"), Index));
        Choice->SetBoolField(TEXT("deprecated"), Enum->HasMetaData(TEXT("Deprecated"), Index));
        Choice->SetBoolField(TEXT("experimental"), Enum->HasMetaData(TEXT("Experimental"), Index));
        Choices.Add(MakeShared<FJsonValueObject>(Choice));
    }
    return Choices;
}
}
