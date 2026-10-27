#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
class UTexture;
struct FLinearColor;

namespace UeNodeNexusBridge
{
// Override writers for MaterialInstanceConstant parameters.
//
// UMaterialEditingLibrary::SetMaterialInstance{Scalar,Vector,Texture,StaticSwitch}
// ParameterValue (UE 5.5, MaterialEditingLibrary.cpp:1061-1180) declare a
// `bResult = false`, apply the value, and return bResult without ever setting it.
// Every call therefore succeeds and reports failure; ue_sync push aborted its
// batch on the first instance parameter because of it. These write through the
// instance directly and confirm by reading back. A parameter the parent material
// does not expose is reported as such instead of being stored as a dead override.
bool SetInstanceScalar(UMaterialInstanceConstant* Instance, FName Name, float Value, FString& OutError);
bool SetInstanceVector(UMaterialInstanceConstant* Instance, FName Name, const FLinearColor& Value, FString& OutError);
bool SetInstanceTexture(UMaterialInstanceConstant* Instance, FName Name, UTexture* Texture, FString& OutError);
bool SetInstanceStaticSwitch(UMaterialInstanceConstant* Instance, FName Name, bool bValue, FString& OutError);
}
