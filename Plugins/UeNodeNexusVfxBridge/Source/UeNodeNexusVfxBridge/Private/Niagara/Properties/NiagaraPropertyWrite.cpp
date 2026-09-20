#include "NiagaraPropertyWrite.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusPropertyValue.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> WriteNiagaraProperties(const FString& Operation, const FString& RequestId,
    UObject* Object, UObject* Asset, const TSharedPtr<FJsonObject>& Payload, const TSharedPtr<FJsonObject>& Data, bool bFull)
{
    bool bDryRun = true, bAllowNonEditable = false, bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("allow_non_editable"), bAllowNonEditable);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    TArray<TSharedPtr<FJsonValue>> Items, Errors;
    TMap<FProperty*, TSharedPtr<FPropertyValueBuffer>> Prepared;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || !Params)
    {
        Errors.Add(MakeShared<FJsonValueObject>(MakeError(TEXT("invalid_request"), TEXT("params must be an array"))));
    }
    else
    {
        for (int32 Index = 0; Index < Params->Num(); ++Index)
        {
            const auto& Value = (*Params)[Index];
            const auto Param = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
            FString Name, ValueText, Error;
            FProperty* Property = nullptr;
            if (!Param.IsValid() || !Param->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
            {
                Error = TEXT("property_name_required");
            }
            else if (!(Property = Object->GetClass()->FindPropertyByName(FName(*Name))))
            {
                Error = TEXT("property_not_found");
            }
            else if (!ShouldExposeProperty(Property, bAllowNonEditable))
            {
                Error = TEXT("property_not_editable");
            }
            else if (Prepared.Contains(Property))
            {
                Error = TEXT("duplicate_property");
            }
            else
            {
                auto Buffer = MakeShared<FPropertyValueBuffer>(Object, Property);
                const auto RawValue = Param->TryGetField(TEXT("value"));
                bool bValid = false;
                if (Param->HasField(TEXT("value_text")))
                {
                    bValid = Param->TryGetStringField(TEXT("value_text"), ValueText) && Buffer->ApplyText(ValueText, Error);
                }
                else if (RawValue.IsValid())
                {
                    bValid = Buffer->ApplyJson(RawValue, ValueText, Error);
                }
                if (bValid)
                {
                    Prepared.Add(Property, Buffer);
                }
                else if (Error.IsEmpty())
                {
                    Error = TEXT("property_value_required_or_invalid");
                }
            }
            auto Item = MakeShared<FJsonObject>();
            Item->SetNumberField(TEXT("index"), Index);
            Item->SetStringField(TEXT("name"), Name);
            Item->SetStringField(TEXT("value_text"), ValueText);
            Item->SetBoolField(TEXT("found"), Property != nullptr);
            Item->SetBoolField(TEXT("applied"), false);
            Item->SetStringField(TEXT("error"), Error);
            Items.Add(MakeShared<FJsonValueObject>(Item));
            if (!Error.IsEmpty())
            {
                Errors.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }
    int32 PlannedChanges = 0;
    for (const auto& Pair : Prepared)
    {
        PlannedChanges += Pair.Value->Changed() ? 1 : 0;
    }
    const bool bApply = Errors.IsEmpty() && !bDryRun && PlannedChanges > 0;
    if (bApply)
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("UE Node Nexus Niagara Properties")));
        Asset->Modify();
        if (Object != Asset)
        {
            Object->Modify();
        }
        Object->PreEditChange(nullptr);
        for (const auto& Pair : Prepared)
        {
            if (Pair.Value->Changed())
            {
                Pair.Value->Commit();
            }
        }
        Object->PostEditChange();
        Object->MarkPackageDirty();
        for (const auto& Item : Items)
        {
            Item->AsObject()->SetBoolField(TEXT("applied"), true);
        }
    }
    const bool bSaveAttempted = Errors.IsEmpty() && !bDryRun && bSave && Asset->GetOutermost()->IsDirty();
    const bool bSaved = bSaveAttempted && SaveAssetPackage(Asset);
    const bool bSaveFailed = bSaveAttempted && !bSaved;
    if (bSaveFailed)
    {
        Errors.Add(MakeShared<FJsonValueObject>(MakeError(TEXT("save_failed"), TEXT("property changes remain in editor memory"))));
    }
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApply);
    Data->SetBoolField(TEXT("changed"), bApply);
    Data->SetBoolField(TEXT("partial"), bApply && bSaveFailed);
    Data->SetNumberField(TEXT("planned_count"), Params ? Params->Num() : 0);
    Data->SetNumberField(TEXT("planned_changed_count"), PlannedChanges);
    Data->SetNumberField(TEXT("changed_count"), bApply ? PlannedChanges : 0);
    Data->SetNumberField(TEXT("failed_count"), Errors.Num());
    Data->SetArrayField(TEXT("errors"), Errors);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetBoolField(TEXT("save_attempted"), bSaveAttempted);
    Data->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());
    Data->SetBoolField(TEXT("details_omitted"), !bFull);
    if (bFull)
    {
        Data->SetArrayField(TEXT("items"), Items);
    }
    auto Response = MakeEnvelope(Operation, RequestId, Errors.IsEmpty());
    Response->SetObjectField(TEXT("data"), Data);
    if (!Errors.IsEmpty())
    {
        Response->SetObjectField(TEXT("error"), MakeError(bSaveFailed ? TEXT("save_failed") : TEXT("invalid_properties"),
            bSaveFailed ? TEXT("properties applied but package save failed") : TEXT("property preflight failed; inspect data.errors")));
    }
    return Response;
}
}
