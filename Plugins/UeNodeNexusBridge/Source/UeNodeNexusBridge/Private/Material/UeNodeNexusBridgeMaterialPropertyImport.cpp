#include "UeNodeNexusBridgeMaterialPropertySchema.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
bool IsEditableMaterialExpressionProperty(FProperty* Property)
{
    return Property != nullptr
        && Property->HasAnyPropertyFlags(CPF_Edit)
        && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
}

bool ImportMaterialExpressionPropertyText(UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, FString& OutOldValue)
{
    if (Expression == nullptr)
    {
        return false;
    }

    FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Name));
    if (!IsEditableMaterialExpressionProperty(Property))
    {
        return false;
    }

    Property->ExportTextItem_InContainer(OutOldValue, Expression, nullptr, Expression, PPF_None);
    if (bDryRun)
    {
        return true;
    }

    return Property->ImportText_InContainer(*Value, Expression, Expression, PPF_None) != nullptr;
}

bool MaterialExpressionJsonValueToPropertyText(UMaterialExpression* Expression, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError)
{
    OutValueText.Empty();
    OutError.Empty();
    if (Expression == nullptr)
    {
        OutError = TEXT("expression_is_null");
        return false;
    }

    FProperty* Property = Expression->GetClass()->FindPropertyByName(FName(*Name));
    if (!IsEditableMaterialExpressionProperty(Property))
    {
        OutError = TEXT("material_expression_property_not_editable_or_missing");
        return false;
    }
    return JsonValueToPropertyImportText(Property, Value, OutValueText, OutError);
}
}
