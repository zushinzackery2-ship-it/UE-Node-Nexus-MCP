#include "NexusMaterialInstanceParameters.h"

#include "Engine/Texture.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UeNodeNexusBridgeMaterialInstanceParamSet.h"

namespace UeNodeNexusBridge::MaterialInstances
{
bool ApplyScalar(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    double Value = 0.0;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetNumberField(TEXT("value"), Value))
    {
        return false;
    }
    FString Error;
    return SetInstanceScalar(Instance, FName(*Name), static_cast<float>(Value), Error);
}

bool ApplyVector(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetObjectField(TEXT("value"), Value) || Value == nullptr)
    {
        return false;
    }

    double R = 0.0;
    double G = 0.0;
    double B = 0.0;
    double A = 1.0;
    (*Value)->TryGetNumberField(TEXT("r"), R);
    (*Value)->TryGetNumberField(TEXT("g"), G);
    (*Value)->TryGetNumberField(TEXT("b"), B);
    (*Value)->TryGetNumberField(TEXT("a"), A);
    FString Error;
    return SetInstanceVector(Instance, FName(*Name), FLinearColor(R, G, B, A), Error);
}

bool ApplyTexture(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    FString Value;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetStringField(TEXT("value"), Value))
    {
        return false;
    }

    UTexture* Texture = LoadObject<UTexture>(nullptr, *Value);
    FString Error;
    return Texture != nullptr && SetInstanceTexture(Instance, FName(*Name), Texture, Error);
}

bool ApplyStaticSwitch(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    bool bValue = false;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetBoolField(TEXT("value"), bValue))
    {
        return false;
    }
    FString Error;
    return SetInstanceStaticSwitch(Instance, FName(*Name), bValue, Error);
}
}
