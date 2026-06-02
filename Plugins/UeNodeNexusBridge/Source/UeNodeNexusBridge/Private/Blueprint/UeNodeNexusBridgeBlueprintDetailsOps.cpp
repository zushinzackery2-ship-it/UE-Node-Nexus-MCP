#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UeNodeNexusBridgeBlueprintComponentJson.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
static FString CompactPinType(const FEdGraphPinType& PinType)
{
    FString Result = PinType.PinCategory.ToString();
    const FString Subcategory = PinType.PinSubCategory.ToString();
    if (!Subcategory.IsEmpty())
    {
        Result += TEXT(":");
        Result += Subcategory;
    }
    if (PinType.PinSubCategoryObject.IsValid())
    {
        Result += TEXT(":");
        Result += PinType.PinSubCategoryObject->GetName();
    }
    return Result;
}

static TSharedPtr<FJsonValue> MakeVariableRow(const FBPVariableDescription& Variable)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Variable.VarName.ToString()));
    Row.Add(MakeShared<FJsonValueString>(CompactPinType(Variable.VarType)));
    Row.Add(MakeShared<FJsonValueString>(Variable.DefaultValue));
    Row.Add(MakeShared<FJsonValueString>(Variable.Category.ToString()));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> MakeVariableObject(const FBPVariableDescription& Variable)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Variable.VarName.ToString());
    Json->SetStringField(TEXT("guid"), Variable.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("type"), CompactPinType(Variable.VarType));
    Json->SetStringField(TEXT("default"), Variable.DefaultValue);
    Json->SetStringField(TEXT("category"), Variable.Category.ToString());
    Json->SetStringField(TEXT("friendly_name"), Variable.FriendlyName);
    return Json;
}

static TSharedPtr<FJsonValue> MakeDefaultRow(const FString& Name, const FString& Value)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Value));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> MakeDefaultObject(const FString& Name, const FString& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Name);
    Json->SetStringField(TEXT("value"), Value);
    return Json;
}

TSharedPtr<FJsonObject> HandleBlueprintDetailsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> ErrorResponse;
    UBlueprint* Blueprint = LoadAssetOrError<UBlueprint>(Payload, Operation, RequestId, ErrorResponse, TEXT("Blueprint"));
    if (Blueprint == nullptr)
    {
        return ErrorResponse;
    }

    UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
    UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;
    if (GeneratedClass == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), TEXT("Blueprint generated class could not be loaded"));
    }

    bool bIncludeDefaults = false;
    bool bIncludeComponents = false;
    bool bIncludeInherited = false;
    Payload->TryGetBoolField(TEXT("include_defaults"), bIncludeDefaults);
    Payload->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    Payload->TryGetBoolField(TEXT("include_inherited_components"), bIncludeInherited);

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Variables;
    TArray<FString> DefaultNames;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        Variables.Add(bCompact ? MakeVariableRow(Variable) : MakeShared<FJsonValueObject>(MakeVariableObject(Variable)));
        DefaultNames.AddUnique(Variable.VarName.ToString());
    }

    TArray<FString> RequestedNames;
    Payload->TryGetStringArrayField(TEXT("property_names"), RequestedNames);
    if (RequestedNames.Num() > 0)
    {
        DefaultNames = RequestedNames;
    }

    TArray<TSharedPtr<FJsonValue>> Defaults;
    TArray<TSharedPtr<FJsonValue>> MissingDefaults;
    if (bIncludeDefaults)
    {
        for (const FString& Name : DefaultNames)
        {
            FString Value;
            if (ExportNamedPropertyText(CDO, FName(*Name), Value))
            {
                Defaults.Add(bCompact ? MakeDefaultRow(Name, Value) : MakeShared<FJsonValueObject>(MakeDefaultObject(Name, Value)));
            }
            else if (RequestedNames.Num() > 0)
            {
                MissingDefaults.Add(MakeShared<FJsonValueString>(Name));
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> Components;
    if (bIncludeComponents)
    {
        CollectBlueprintComponents(GeneratedClass, CDO, bCompact, bIncludeInherited, Components);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetPathName());
    Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
    Data->SetStringField(TEXT("generated_class"), GeneratedClass->GetPathName());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("blueprint_details_compact"));
        Data->SetArrayField(TEXT("variable_columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("type")), MakeShared<FJsonValueString>(TEXT("default")), MakeShared<FJsonValueString>(TEXT("category")) });
        Data->SetArrayField(TEXT("default_columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("value")) });
        Data->SetArrayField(TEXT("component_columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("parent")), MakeShared<FJsonValueString>(TEXT("socket")), MakeShared<FJsonValueString>(TEXT("asset")), MakeShared<FJsonValueString>(TEXT("origin")) });
    }
    Data->SetArrayField(TEXT("variables"), Variables);
    Data->SetArrayField(TEXT("defaults"), Defaults);
    Data->SetArrayField(TEXT("missing_defaults"), MissingDefaults);
    Data->SetArrayField(TEXT("components"), Components);
    Data->SetNumberField(TEXT("variable_count"), Variables.Num());
    Data->SetNumberField(TEXT("default_count"), Defaults.Num());
    Data->SetNumberField(TEXT("component_count"), Components.Num());
    AddElapsedMs(Data, StartSeconds);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
