// Editor globals stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "UObject/Object.h"

class FViewport;
class UWorld;

struct FWorldContext
{
    UWorld* World() const;
};

class UEditorEngine : public UObject
{
public:
    template <typename SubsystemType>
    SubsystemType* GetEditorSubsystem()
    {
        return nullptr;
    }

    FWorldContext& GetEditorWorldContext(bool bEnsureIsGWorld = false);
    FViewport* GetActiveViewport();
    void RedrawAllViewports(bool bInvalidateHitProxies = true);
};

extern UEditorEngine* GEditor;
