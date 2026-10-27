#include "UeNodeNexusBridgeTranscode.h"

#include "Materials/MaterialExpression.h"

namespace UeNodeNexusBridge::Transcode
{
FString MaterialExpressionKey(UMaterialExpression* Expression, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions)
{
    if (Expression == nullptr)
    {
        return FString();
    }
    // MaterialExpressionGuid is editor-only data that legacy assets can repeat across
    // expressions; the object name disambiguates those so no node is lost or confused.
    const FGuid Id = Expression->GetMaterialExpressionId();
    int32 Shared = 0;
    for (const TObjectPtr<UMaterialExpression>& Item : Expressions)
    {
        if (Item != nullptr && Item->GetMaterialExpressionId() == Id)
        {
            ++Shared;
        }
    }
    const FString Text = Id.ToString(EGuidFormats::DigitsWithHyphens);
    return Shared > 1 ? Text + TEXT("#") + Expression->GetName() : Text;
}
}
