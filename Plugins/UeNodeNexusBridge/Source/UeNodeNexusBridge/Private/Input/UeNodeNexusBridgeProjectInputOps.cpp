#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
static FKey ReadKey(const FString& KeyName)
{
    return FKey(FName(*KeyName));
}

static TSharedPtr<FJsonObject> MakeAxisMappingItem(const FInputAxisKeyMapping& Mapping)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("axis_name"), Mapping.AxisName.ToString());
    Item->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());
    Item->SetNumberField(TEXT("scale"), Mapping.Scale);
    return Item;
}

static TSharedPtr<FJsonObject> MakeActionMappingItem(const FInputActionKeyMapping& Mapping)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("action_name"), Mapping.ActionName.ToString());
    Item->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());
    Item->SetBoolField(TEXT("shift"), Mapping.bShift);
    Item->SetBoolField(TEXT("ctrl"), Mapping.bCtrl);
    Item->SetBoolField(TEXT("alt"), Mapping.bAlt);
    Item->SetBoolField(TEXT("cmd"), Mapping.bCmd);
    return Item;
}

static bool AxisMappingEquals(const FInputAxisKeyMapping& Left, const FInputAxisKeyMapping& Right)
{
    return Left.AxisName == Right.AxisName && Left.Key == Right.Key && FMath::IsNearlyEqual(Left.Scale, Right.Scale);
}

static bool ActionMappingEquals(const FInputActionKeyMapping& Left, const FInputActionKeyMapping& Right)
{
    return Left.ActionName == Right.ActionName
        && Left.Key == Right.Key
        && Left.bShift == Right.bShift
        && Left.bCtrl == Right.bCtrl
        && Left.bAlt == Right.bAlt
        && Left.bCmd == Right.bCmd;
}

static void ReadMappings(UInputSettings* Settings, TArray<FInputAxisKeyMapping>& AxisMappings, TArray<FInputActionKeyMapping>& ActionMappings)
{
    if (Settings != nullptr)
    {
        AxisMappings = Settings->GetAxisMappings();
        ActionMappings = Settings->GetActionMappings();
    }
}

static TSharedPtr<FJsonObject> MakeMappingsData(UInputSettings* Settings, const FString& Format)
{
    TArray<FInputAxisKeyMapping> AxisMappings;
    TArray<FInputActionKeyMapping> ActionMappings;
    ReadMappings(Settings, AxisMappings, ActionMappings);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format == TEXT("full") ? TEXT("project_input_mappings_full") : TEXT("project_input_mappings_compact"));
    Data->SetNumberField(TEXT("axis_count"), AxisMappings.Num());
    Data->SetNumberField(TEXT("action_count"), ActionMappings.Num());

    TArray<TSharedPtr<FJsonValue>> AxisItems;
    TArray<TSharedPtr<FJsonValue>> ActionItems;
    FString Text = FString::Printf(TEXT("I:axis=%d|action=%d\n"), AxisMappings.Num(), ActionMappings.Num());
    for (int32 Index = 0; Index < AxisMappings.Num(); ++Index)
    {
        AxisItems.Add(MakeShared<FJsonValueObject>(MakeAxisMappingItem(AxisMappings[Index])));
        Text += FString::Printf(TEXT("A:%d:%s|%s|%g\n"), Index, *AxisMappings[Index].AxisName.ToString(), *AxisMappings[Index].Key.GetFName().ToString(), AxisMappings[Index].Scale);
    }
    for (int32 Index = 0; Index < ActionMappings.Num(); ++Index)
    {
        ActionItems.Add(MakeShared<FJsonValueObject>(MakeActionMappingItem(ActionMappings[Index])));
        Text += FString::Printf(TEXT("K:%d:%s|%s|shift=%d|ctrl=%d|alt=%d|cmd=%d\n"), Index, *ActionMappings[Index].ActionName.ToString(), *ActionMappings[Index].Key.GetFName().ToString(), ActionMappings[Index].bShift ? 1 : 0, ActionMappings[Index].bCtrl ? 1 : 0, ActionMappings[Index].bAlt ? 1 : 0, ActionMappings[Index].bCmd ? 1 : 0);
    }

    if (Format == TEXT("full"))
    {
        Data->SetArrayField(TEXT("axis_mappings"), AxisItems);
        Data->SetArrayField(TEXT("action_mappings"), ActionItems);
    }
    Data->SetStringField(TEXT("text"), Text);
    Data->SetNumberField(TEXT("text_bytes"), Text.Len());
    TArray<FString> Lines;
    Data->SetNumberField(TEXT("text_lines"), Text.ParseIntoArrayLines(Lines));
    return Data;
}

static bool ReadAxisMapping(const TSharedPtr<FJsonObject>& Op, FInputAxisKeyMapping& OutMapping)
{
    FString AxisName;
    FString KeyName;
    double Scale = 1.0;
    if (!Op->TryGetStringField(TEXT("axis_name"), AxisName) || !Op->TryGetStringField(TEXT("key"), KeyName))
    {
        return false;
    }
    Op->TryGetNumberField(TEXT("scale"), Scale);
    OutMapping = FInputAxisKeyMapping(FName(*AxisName), ReadKey(KeyName), static_cast<float>(Scale));
    return true;
}

static bool ReadActionMapping(const TSharedPtr<FJsonObject>& Op, FInputActionKeyMapping& OutMapping)
{
    FString ActionName;
    FString KeyName;
    if (!Op->TryGetStringField(TEXT("action_name"), ActionName) || !Op->TryGetStringField(TEXT("key"), KeyName))
    {
        return false;
    }
    OutMapping = FInputActionKeyMapping(FName(*ActionName), ReadKey(KeyName));
    bool bShift = OutMapping.bShift;
    bool bCtrl = OutMapping.bCtrl;
    bool bAlt = OutMapping.bAlt;
    bool bCmd = OutMapping.bCmd;
    Op->TryGetBoolField(TEXT("shift"), bShift);
    Op->TryGetBoolField(TEXT("ctrl"), bCtrl);
    Op->TryGetBoolField(TEXT("alt"), bAlt);
    Op->TryGetBoolField(TEXT("cmd"), bCmd);
    OutMapping.bShift = bShift;
    OutMapping.bCtrl = bCtrl;
    OutMapping.bAlt = bAlt;
    OutMapping.bCmd = bCmd;
    return true;
}

static bool HasAxisMapping(UInputSettings* Settings, const FInputAxisKeyMapping& Mapping)
{
    TArray<FInputAxisKeyMapping> Existing;
    Existing = Settings->GetAxisMappings();
    return Existing.ContainsByPredicate([&Mapping](const FInputAxisKeyMapping& Item)
    {
        return AxisMappingEquals(Item, Mapping);
    });
}

static bool HasActionMapping(UInputSettings* Settings, const FInputActionKeyMapping& Mapping)
{
    TArray<FInputActionKeyMapping> Existing;
    Existing = Settings->GetActionMappings();
    return Existing.ContainsByPredicate([&Mapping](const FInputActionKeyMapping& Item)
    {
        return ActionMappingEquals(Item, Mapping);
    });
}

static bool ApplyInputMappingOperation(UInputSettings* Settings, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Data)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        return false;
    }

    if (OpName == TEXT("add_axis_mapping") || OpName == TEXT("remove_axis_mapping"))
    {
        FInputAxisKeyMapping Mapping;
        if (!ReadAxisMapping(Op, Mapping))
        {
            return false;
        }
        const bool bExists = HasAxisMapping(Settings, Mapping);
        const bool bAdd = OpName == TEXT("add_axis_mapping");
        if (!bDryRun && bAdd && !bExists)
        {
            Settings->AddAxisMapping(Mapping, false);
        }
        if (!bDryRun && !bAdd && bExists)
        {
            Settings->RemoveAxisMapping(Mapping, false);
        }
        Data->SetNumberField(bAdd ? TEXT("axis_added") : TEXT("axis_removed"), Data->GetNumberField(bAdd ? TEXT("axis_added") : TEXT("axis_removed")) + (bAdd != bExists ? 1 : 0));
        return true;
    }

    if (OpName == TEXT("add_action_mapping") || OpName == TEXT("remove_action_mapping"))
    {
        FInputActionKeyMapping Mapping;
        if (!ReadActionMapping(Op, Mapping))
        {
            return false;
        }
        const bool bExists = HasActionMapping(Settings, Mapping);
        const bool bAdd = OpName == TEXT("add_action_mapping");
        if (!bDryRun && bAdd && !bExists)
        {
            Settings->AddActionMapping(Mapping, false);
        }
        if (!bDryRun && !bAdd && bExists)
        {
            Settings->RemoveActionMapping(Mapping, false);
        }
        Data->SetNumberField(bAdd ? TEXT("actions_added") : TEXT("actions_removed"), Data->GetNumberField(bAdd ? TEXT("actions_added") : TEXT("actions_removed")) + (bAdd != bExists ? 1 : 0));
        return true;
    }
    return false;
}

TSharedPtr<FJsonObject> HandleProjectInputMappingsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeMappingsData(UInputSettings::GetInputSettings(), Format));
    return Response;
}

TSharedPtr<FJsonObject> HandleProjectInputMappingsPatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations must be an array")));
        return Response;
    }

    bool bDryRun = true;
    bool bSaveConfig = true;
    FString Format = TEXT("compact");
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save_config"), bSaveConfig);
    Payload->TryGetStringField(TEXT("format"), Format);

    UInputSettings* Settings = UInputSettings::GetInputSettings();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("save_config"), bSaveConfig);
    Data->SetBoolField(TEXT("config_saved"), false);
    Data->SetStringField(TEXT("config_file"), FString());
    Data->SetNumberField(TEXT("axis_added"), 0);
    Data->SetNumberField(TEXT("axis_removed"), 0);
    Data->SetNumberField(TEXT("actions_added"), 0);
    Data->SetNumberField(TEXT("actions_removed"), 0);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedPtr<FJsonValue>& Value : *Operations)
    {
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyInputMappingOperation(Settings, Op, bDryRun, Data))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("input_mapping_operation_failed"), TEXT("Project input mapping operation failed validation or application"), TEXT("ProjectSettings"), TEXT("UeNodeNexusBridge"))));
        }
    }

    const int32 ChangedCount = static_cast<int32>(Data->GetNumberField(TEXT("axis_added")) + Data->GetNumberField(TEXT("axis_removed")) + Data->GetNumberField(TEXT("actions_added")) + Data->GetNumberField(TEXT("actions_removed")));
    Data->SetBoolField(TEXT("changed"), ChangedCount > 0);
    if (!bDryRun && ChangedCount > 0)
    {
        Settings->ForceRebuildKeymaps();
        if (bSaveConfig)
        {
            Settings->SaveKeyMappings();
            FString ConfigFile;
            const bool bConfigSaved = SaveObjectConfig(Settings, ConfigFile);
            Data->SetBoolField(TEXT("config_saved"), bConfigSaved);
            Data->SetStringField(TEXT("config_file"), ConfigFile);
        }
    }
    Data->SetObjectField(TEXT("mappings"), MakeMappingsData(Settings, Format));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Diagnostics.Num() == 0);
    Response->SetObjectField(TEXT("data"), Data);
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
