// Editor globals stub for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/Array.h"
#include "UObject/Object.h"

class FLevelEditorViewportClient;
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
    const TArray<FLevelEditorViewportClient*>& GetLevelViewportClients();
    void RedrawAllViewports(bool bInvalidateHitProxies = true);
};

extern UEditorEngine* GEditor;
