#include "Material/Creation/NexusMaterialExpressionCreate.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionTransformPosition.h"

namespace UeNodeNexusBridge
{
UMaterialExpression* CreateNexusMaterialExpression(UObject* Owner, UClass* Class, int32 X, int32 Y)
{
    UMaterialExpression* Expression = nullptr;
    if (UMaterial* Material = Cast<UMaterial>(Owner))
    {
        Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
    }
    else if (UMaterialFunction* Function = Cast<UMaterialFunction>(Owner))
    {
        Expression = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, Class, X, Y);
    }
    if (Expression == nullptr)
    {
        return nullptr;
    }
    // The editor's placement defaults differ from reflection's CDO defaults.
    // Every bridge creation path starts with the defaults advertised by schema.
    const UObject* Defaults = Expression->GetClass()->GetDefaultObject();
    if (auto* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
    {
        const auto* Cdo = CastChecked<UMaterialExpressionComponentMask>(Defaults);
        Mask->R = Cdo->R;
        Mask->G = Cdo->G;
        Mask->B = Cdo->B;
        Mask->A = Cdo->A;
    }
    if (auto* Mask = Cast<UMaterialExpressionStaticComponentMaskParameter>(Expression))
    {
        const auto* Cdo = CastChecked<UMaterialExpressionStaticComponentMaskParameter>(Defaults);
        Mask->DefaultR = Cdo->DefaultR;
        Mask->DefaultG = Cdo->DefaultG;
        Mask->DefaultB = Cdo->DefaultB;
        Mask->DefaultA = Cdo->DefaultA;
    }
    if (auto* Transform = Cast<UMaterialExpressionTransformPosition>(Expression))
    {
        const auto* Cdo = CastChecked<UMaterialExpressionTransformPosition>(Defaults);
        Transform->TransformSourceType = Cdo->TransformSourceType;
        Transform->TransformType = Cdo->TransformType;
    }
    return Expression;
}
}
