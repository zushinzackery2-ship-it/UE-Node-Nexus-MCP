#include "UeNodeNexusBridgeMaterialPropertySchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static FString ExportMaterialExpressionPropertyValue(UMaterialExpression* Expression, FProperty* Property)
{
    FString Value;
    if (Expression != nullptr && Property != nullptr)
    {
        Property->ExportTextItem_InContainer(Value, Expression, nullptr, Expression, PPF_None);
    }
    return Value;
}

static bool ShouldSkipMaterialSnapshotParamValue(const FString& Name, const FString& Value)
{
    return Value.IsEmpty()
        && (Name.Equals(TEXT("Desc"), ESearchCase::CaseSensitive)
            || Name.Equals(TEXT("Description"), ESearchCase::CaseSensitive));
}

TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParamValues(UMaterialExpression* Expression)
{
    TArray<TSharedPtr<FJsonValue>> Params;
    if (Expression == nullptr)
    {
        return Params;
    }

    int32 Index = 0;
    for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsEditableMaterialExpressionProperty(Property))
        {
            continue;
        }

        const FString Value = ExportMaterialExpressionPropertyValue(Expression, Property);
        if (ShouldSkipMaterialSnapshotParamValue(Property->GetName(), Value))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("index"), Index++);
        Json->SetStringField(TEXT("name"), Property->GetName());
        Json->SetStringField(TEXT("type"), Property->GetCPPType());
        Json->SetStringField(TEXT("value"), Value);
        Json->SetBoolField(TEXT("editable"), true);
        Params.Add(MakeShared<FJsonValueObject>(Json));
    }

    if (UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("index"), Index);
        Json->SetStringField(TEXT("name"), TEXT("DeclarationName"));
        Json->SetStringField(TEXT("type"), TEXT("FName"));
        Json->SetStringField(TEXT("value"), Usage->Declaration ? Usage->Declaration->Name.ToString() : FString());
        Json->SetBoolField(TEXT("editable"), true);
        Params.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Params;
}
}
