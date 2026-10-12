#include "NexusRestoreRenderScope.h"

#include "Engine/TextureRenderTarget.h"
#include "RenderCommandFence.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge::Collaboration
{
static void DrainRenderResources()
{
    FRenderCommandFence Fence;
    Fence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
    Fence.Wait(false);
}

FRestoreRenderScope::FRestoreRenderScope()
    : Started(FPlatformTime::Seconds())
{
    check(IsInGameThread());
    for (TObjectIterator<UTextureRenderTarget> It; It; ++It)
    {
        if (IsValid(*It) && It->GetOutermost() == GetTransientPackage())
        {
            Targets.Emplace(*It);
        }
    }
    if (!Targets.IsEmpty())
    {
        DrainRenderResources();
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus phase=restore_render_begin transient_targets=%d"), Targets.Num());
}

FRestoreRenderScope::~FRestoreRenderScope()
{
    if (!Targets.IsEmpty())
    {
        DrainRenderResources();
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus phase=restore_render_complete transient_targets=%d elapsed_ms=%.3f"),
        Targets.Num(), 1000.0 * (FPlatformTime::Seconds() - Started));
}

int32 FRestoreRenderScope::Num() const
{
    return Targets.Num();
}
}
