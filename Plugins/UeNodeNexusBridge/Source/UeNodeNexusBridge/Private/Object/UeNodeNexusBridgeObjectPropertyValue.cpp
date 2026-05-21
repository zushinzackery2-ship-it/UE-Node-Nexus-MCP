#include "UeNodeNexusBridgeObjectHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool ReadObjectNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Name, double& OutValue)
{
    return Json.IsValid() && Json->TryGetNumberField(Name, OutValue);
}

static bool ReadVectorFields(const TSharedPtr<FJsonObject>& Json, FVector& OutValue)
{
    double X = OutValue.X;
    double Y = OutValue.Y;
    double Z = OutValue.Z;
    const bool bAny = ReadObjectNumber(Json, TEXT("x"), X) | ReadObjectNumber(Json, TEXT("X"), X) |
        ReadObjectNumber(Json, TEXT("y"), Y) | ReadObjectNumber(Json, TEXT("Y"), Y) |
        ReadObjectNumber(Json, TEXT("z"), Z) | ReadObjectNumber(Json, TEXT("Z"), Z);
    OutValue = FVector(X, Y, Z);
    return bAny;
}

static bool ReadRotatorFields(const TSharedPtr<FJsonObject>& Json, FRotator& OutValue)
{
    double Pitch = OutValue.Pitch;
    double Yaw = OutValue.Yaw;
    double Roll = OutValue.Roll;
    const bool bAny = ReadObjectNumber(Json, TEXT("pitch"), Pitch) | ReadObjectNumber(Json, TEXT("Pitch"), Pitch) |
        ReadObjectNumber(Json, TEXT("yaw"), Yaw) | ReadObjectNumber(Json, TEXT("Yaw"), Yaw) |
        ReadObjectNumber(Json, TEXT("roll"), Roll) | ReadObjectNumber(Json, TEXT("Roll"), Roll);
    OutValue = FRotator(Pitch, Yaw, Roll);
    return bAny;
}

static bool ReadColorFields(const TSharedPtr<FJsonObject>& Json, FLinearColor& OutValue)
{
    double R = OutValue.R;
    double G = OutValue.G;
    double B = OutValue.B;
    double A = OutValue.A;
    const bool bAny = ReadObjectNumber(Json, TEXT("r"), R) | ReadObjectNumber(Json, TEXT("R"), R) |
        ReadObjectNumber(Json, TEXT("g"), G) | ReadObjectNumber(Json, TEXT("G"), G) |
        ReadObjectNumber(Json, TEXT("b"), B) | ReadObjectNumber(Json, TEXT("B"), B) |
        ReadObjectNumber(Json, TEXT("a"), A) | ReadObjectNumber(Json, TEXT("A"), A);
    OutValue = FLinearColor(R, G, B, A);
    return bAny;
}

static FString VectorImportText(const FVector& Value)
{
    return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"), *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y), *FString::SanitizeFloat(Value.Z));
}

static FString RotatorImportText(const FRotator& Value)
{
    return FString::Printf(TEXT("(Pitch=%s,Yaw=%s,Roll=%s)"), *FString::SanitizeFloat(Value.Pitch), *FString::SanitizeFloat(Value.Yaw), *FString::SanitizeFloat(Value.Roll));
}

static FString ColorImportText(const FLinearColor& Value)
{
    return FString::Printf(
        TEXT("(R=%s,G=%s,B=%s,A=%s)"),
        *FString::SanitizeFloat(Value.R),
        *FString::SanitizeFloat(Value.G),
        *FString::SanitizeFloat(Value.B),
        *FString::SanitizeFloat(Value.A));
}

static bool TryBuildStructImportText(FStructProperty* StructProperty, const TSharedPtr<FJsonObject>& Json, FString& OutValueText)
{
    if (StructProperty->Struct == TBaseStructure<FVector>::Get())
    {
        FVector Value = FVector::ZeroVector;
        if (!ReadVectorFields(Json, Value))
        {
            return false;
        }
        OutValueText = VectorImportText(Value);
        return true;
    }
    if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
    {
        FRotator Value = FRotator::ZeroRotator;
        if (!ReadRotatorFields(Json, Value))
        {
            return false;
        }
        OutValueText = RotatorImportText(Value);
        return true;
    }
    if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
    {
        FLinearColor Value = FLinearColor::Transparent;
        if (!ReadColorFields(Json, Value))
        {
            return false;
        }
        OutValueText = ColorImportText(Value);
        return true;
    }
    return false;
}

bool JsonValueToPropertyImportText(FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError)
{
    OutValueText.Empty();
    OutError.Empty();
    if (Property == nullptr || !Value.IsValid())
    {
        OutError = TEXT("missing_property_or_value");
        return false;
    }

    TSharedPtr<FJsonObject> Json = Value->AsObject();
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        if (Json.IsValid() && TryBuildStructImportText(StructProperty, Json, OutValueText))
        {
            return true;
        }
        OutError = TEXT("unsupported_struct_json_value");
        return false;
    }

    OutValueText = JsonValueToImportText(Value);
    if (OutValueText.IsEmpty() && Json.IsValid())
    {
        OutError = TEXT("unsupported_json_object_value");
        return false;
    }
    return true;
}

bool ApplyPropertyJsonValue(UObject* Object, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError)
{
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        FString Path;
        if (!Value.IsValid() || !Value->TryGetString(Path))
        {
            OutError = TEXT("object_property_value_must_be_path_string");
            return false;
        }
        UObject* NewValue = Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? nullptr : ResolveObjectByPath(Path);
        if (NewValue == nullptr && !Path.IsEmpty() && !Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            OutError = TEXT("object_property_value_not_found");
            return false;
        }
        if (NewValue != nullptr && ObjectProperty->PropertyClass != nullptr && !NewValue->IsA(ObjectProperty->PropertyClass))
        {
            OutError = TEXT("object_property_class_mismatch");
            return false;
        }
        ObjectProperty->SetObjectPropertyValue_InContainer(Object, NewValue);
        OutValueText = NewValue ? NewValue->GetPathName() : TEXT("None");
        return true;
    }

    if (!JsonValueToPropertyImportText(Property, Value, OutValueText, OutError))
    {
        return false;
    }
    return ApplyPropertyText(Object, Property, OutValueText);
}
}
