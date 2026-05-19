#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/UnrealType.h"

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

static FString CleanExportedText(FString Value)
{
    Value.RemoveFromStart(TEXT("("));
    Value.RemoveFromEnd(TEXT(")"));
    Value.ReplaceInline(TEXT("\r"), TEXT(" "));
    Value.ReplaceInline(TEXT("\n"), TEXT(" "));
    return Value.Left(300);
}

static bool ExportPropertyText(UObject* Object, const FName& PropertyName, FString& OutValue)
{
    FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(PropertyName) : nullptr;
    if (Property == nullptr)
    {
        return false;
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* ValueObject = ObjectProperty->GetObjectPropertyValue_InContainer(Object);
        OutValue = ValueObject ? ValueObject->GetPathName() : FString();
        return true;
    }

    FString Value;
    Property->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
    OutValue = CleanExportedText(Value);
    return true;
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

static FString FirstComponentAsset(UActorComponent* Component)
{
    static const FName Names[] = {
        TEXT("StaticMesh"),
        TEXT("SkeletalMeshAsset"),
        TEXT("SkeletalMesh"),
        TEXT("AnimClass"),
        TEXT("Texture")
    };

    for (const FName& Name : Names)
    {
        FString Value;
        if (ExportPropertyText(Component, Name, Value) && !Value.IsEmpty() && Value != TEXT("None"))
        {
            return Value;
        }
    }
    return FString();
}

static TSharedPtr<FJsonValue> MakeComponentRow(const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Component ? Component->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Parent));
    Row.Add(MakeShared<FJsonValueString>(Socket));
    Row.Add(MakeShared<FJsonValueString>(FirstComponentAsset(Component)));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> MakeComponentObject(const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Name);
    Json->SetStringField(TEXT("class"), Component ? Component->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("parent"), Parent);
    Json->SetStringField(TEXT("socket"), Socket);
    Json->SetStringField(TEXT("asset"), FirstComponentAsset(Component));
    Json->SetStringField(TEXT("template_path"), Component ? Component->GetPathName() : FString());
    return Json;
}

static void AddComponentEntry(TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames, bool bCompact, const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket)
{
    if (Name.IsEmpty() || SeenNames.Contains(Name))
    {
        return;
    }

    SeenNames.Add(Name);
    Components.Add(bCompact ? MakeComponentRow(Name, Component, Parent, Socket) : MakeShared<FJsonValueObject>(MakeComponentObject(Name, Component, Parent, Socket)));
}

static void AddSCSComponents(UBlueprintGeneratedClass* GeneratedClass, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames)
{
    USimpleConstructionScript* Script = GeneratedClass ? GeneratedClass->SimpleConstructionScript.Get() : nullptr;
    if (Script == nullptr)
    {
        return;
    }

    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node == nullptr)
        {
            continue;
        }

        UActorComponent* Component = Node->GetActualComponentTemplate(GeneratedClass);
        AddComponentEntry(Components, SeenNames, bCompact, Node->GetVariableName().ToString(), Component, Node->ParentComponentOrVariableName.ToString(), Node->AttachToName.ToString());
    }
}

static void AddCDOComponentProperties(UBlueprintGeneratedClass* GeneratedClass, UObject* CDO, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames)
{
    if (GeneratedClass == nullptr || CDO == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectProperty> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectProperty* Property = *It;
        if (Property == nullptr || Property->PropertyClass == nullptr || !Property->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
        {
            continue;
        }

        UActorComponent* Component = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(CDO));
        AddComponentEntry(Components, SeenNames, bCompact, Property->GetName(), Component, FString(), FString());
    }
}

TSharedPtr<FJsonObject> HandleBlueprintDetailsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    UBlueprintGeneratedClass* GeneratedClass = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get()) : nullptr;
    UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;
    if (Blueprint == nullptr || GeneratedClass == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Blueprint with generated class could not be loaded")));
        return Response;
    }

    bool bIncludeDefaults = true;
    bool bIncludeComponents = true;
    Payload->TryGetBoolField(TEXT("include_defaults"), bIncludeDefaults);
    Payload->TryGetBoolField(TEXT("include_components"), bIncludeComponents);

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
            if (ExportPropertyText(CDO, FName(*Name), Value))
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
        TSet<FString> SeenNames;
        AddSCSComponents(GeneratedClass, bCompact, Components, SeenNames);
        AddCDOComponentProperties(GeneratedClass, CDO, bCompact, Components, SeenNames);
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
        Data->SetArrayField(TEXT("component_columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("parent")), MakeShared<FJsonValueString>(TEXT("socket")), MakeShared<FJsonValueString>(TEXT("asset")) });
    }
    Data->SetArrayField(TEXT("variables"), Variables);
    Data->SetArrayField(TEXT("defaults"), Defaults);
    Data->SetArrayField(TEXT("missing_defaults"), MissingDefaults);
    Data->SetArrayField(TEXT("components"), Components);
    Data->SetNumberField(TEXT("variable_count"), Variables.Num());
    Data->SetNumberField(TEXT("default_count"), Defaults.Num());
    Data->SetNumberField(TEXT("component_count"), Components.Num());
    Data->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
