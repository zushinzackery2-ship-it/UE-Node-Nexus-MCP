#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UTextureRenderTarget;

namespace UeNodeNexusBridge::Collaboration
{
// Package reload collects garbage while render proxies still hold resources.
// Keep transient render targets alive until the reload's RHI commands finish.
class FRestoreRenderScope
{
public:
    FRestoreRenderScope();
    ~FRestoreRenderScope();
    int32 Num() const;

private:
    TArray<TStrongObjectPtr<UTextureRenderTarget>> Targets;
    double Started;
};
}
