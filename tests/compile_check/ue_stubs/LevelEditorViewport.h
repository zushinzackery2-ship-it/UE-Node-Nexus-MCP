// FLevelEditorViewportClient / GCurrentLevelEditingViewportClient stubs for
// compiler-only checks; mirrors Editor/UnrealEd/Public/LevelEditorViewport.h.
#pragma once

#include "Math/UeMathTypes.h"

class FViewport;

class FLevelEditorViewportClient
{
public:
    FViewport* Viewport = nullptr;
    float ViewFOV = 90.0f;

    bool IsPerspective() const;
    bool IsRealtime() const;
    FVector GetViewLocation() const;
    FRotator GetViewRotation() const;
    void SetViewLocation(const FVector& NewLocation);
    void SetViewRotation(const FRotator& NewRotation);
    void Invalidate(bool bInvalidateChildViews = true, bool bInvalidateHitProxies = true);
};

extern FLevelEditorViewportClient* GCurrentLevelEditingViewportClient;
