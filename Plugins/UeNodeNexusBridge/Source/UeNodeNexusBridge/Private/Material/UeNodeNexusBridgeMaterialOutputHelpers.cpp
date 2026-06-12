#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"

#include "MaterialExpressionIO.h"
#include "Materials/Material.h"

namespace UeNodeNexusBridge
{
FString MaterialOutputNodeId()
{
    return TEXT("MaterialOutput");
}

bool IsMaterialOutputNodeId(const FString& NodeId)
{
    return NodeId.Equals(TEXT("MaterialOutput"), ESearchCase::IgnoreCase)
        || NodeId.Equals(TEXT("Output"), ESearchCase::IgnoreCase)
        || NodeId.Equals(TEXT("Root"), ESearchCase::IgnoreCase);
}

TArray<EMaterialProperty> MaterialOutputProperties()
{
    return {
        MP_MaterialAttributes,
        MP_BaseColor,
        MP_Metallic,
        MP_Specular,
        MP_Roughness,
        MP_Anisotropy,
        MP_EmissiveColor,
        MP_Opacity,
        MP_OpacityMask,
        MP_Normal,
        MP_Tangent,
        MP_WorldPositionOffset,
        MP_SubsurfaceColor,
        MP_AmbientOcclusion,
        MP_Refraction,
        MP_CustomizedUVs0,
        MP_CustomizedUVs1,
        MP_CustomizedUVs2,
        MP_CustomizedUVs3,
        MP_CustomizedUVs4,
        MP_CustomizedUVs5,
        MP_CustomizedUVs6,
        MP_CustomizedUVs7,
        MP_PixelDepthOffset,
        MP_ShadingModel,
        MP_Displacement
    };
}

FString MaterialOutputPropertyName(EMaterialProperty Property)
{
    switch (Property)
    {
    case MP_MaterialAttributes: return TEXT("MaterialAttributes");
    case MP_BaseColor: return TEXT("BaseColor");
    case MP_Metallic: return TEXT("Metallic");
    case MP_Specular: return TEXT("Specular");
    case MP_Roughness: return TEXT("Roughness");
    case MP_Anisotropy: return TEXT("Anisotropy");
    case MP_EmissiveColor: return TEXT("EmissiveColor");
    case MP_Opacity: return TEXT("Opacity");
    case MP_OpacityMask: return TEXT("OpacityMask");
    case MP_Normal: return TEXT("Normal");
    case MP_Tangent: return TEXT("Tangent");
    case MP_WorldPositionOffset: return TEXT("WorldPositionOffset");
    case MP_SubsurfaceColor: return TEXT("SubsurfaceColor");
    case MP_AmbientOcclusion: return TEXT("AmbientOcclusion");
    case MP_Refraction: return TEXT("Refraction");
    case MP_CustomizedUVs0: return TEXT("CustomizedUVs_0");
    case MP_CustomizedUVs1: return TEXT("CustomizedUVs_1");
    case MP_CustomizedUVs2: return TEXT("CustomizedUVs_2");
    case MP_CustomizedUVs3: return TEXT("CustomizedUVs_3");
    case MP_CustomizedUVs4: return TEXT("CustomizedUVs_4");
    case MP_CustomizedUVs5: return TEXT("CustomizedUVs_5");
    case MP_CustomizedUVs6: return TEXT("CustomizedUVs_6");
    case MP_CustomizedUVs7: return TEXT("CustomizedUVs_7");
    case MP_PixelDepthOffset: return TEXT("PixelDepthOffset");
    case MP_ShadingModel: return TEXT("ShadingModel");
    case MP_Displacement: return TEXT("Displacement");
    default: return FString();
    }
}

FExpressionInput* ResolveMaterialOutputInput(UMaterial* Material, const FString& PinId)
{
    if (Material == nullptr)
    {
        return nullptr;
    }

    EMaterialProperty Property = MP_MAX;
    return ResolveMaterialOutputProperty(PinId, Property) ? Material->GetExpressionInputForProperty(Property) : nullptr;
}

bool ResolveMaterialOutputProperty(const FString& PinId, EMaterialProperty& OutProperty)
{
    FString Target = PinId;
    Target.TrimStartAndEndInline();
    if (Target.IsNumeric())
    {
        const TArray<EMaterialProperty> Properties = MaterialOutputProperties();
        const int32 Index = FCString::Atoi(*Target);
        if (Properties.IsValidIndex(Index))
        {
            OutProperty = Properties[Index];
            return true;
        }
        return false;
    }

    Target.RemoveFromStart(TEXT("MP_"), ESearchCase::IgnoreCase);
    Target.ReplaceInline(TEXT(" "), TEXT(""));
    Target.ReplaceInline(TEXT("_"), TEXT(""));
    for (EMaterialProperty Property : MaterialOutputProperties())
    {
        FString Name = MaterialOutputPropertyName(Property);
        FString NormalizedName = Name;
        NormalizedName.ReplaceInline(TEXT("_"), TEXT(""));
        if (NormalizedName.Equals(Target, ESearchCase::IgnoreCase))
        {
            OutProperty = Property;
            return true;
        }
    }
    return false;
}
}
