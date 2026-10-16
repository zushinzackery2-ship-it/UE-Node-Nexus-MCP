#pragma once

#include "CoreMinimal.h"

class FPackageReloadedEvent;
class UPackage;
enum class EPackageReloadPhase : uint8;

namespace UeNodeNexusBridge::Collaboration
{
class FRestoreBlueprintScope
{
public:
    explicit FRestoreBlueprintScope(const TArray<UPackage*>& InPackages);
    ~FRestoreBlueprintScope();

    int32 Num() const
    {
        return RetiredCount;
    }

private:
    void OnReload(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);

    TArray<UPackage*> Packages;
    FDelegateHandle Handle;
    int32 RetiredCount = 0;
};
}
