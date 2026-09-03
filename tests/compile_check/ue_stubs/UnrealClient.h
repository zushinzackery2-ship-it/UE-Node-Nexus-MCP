// FViewport / FScreenshotRequest stubs for compiler-only checks. The
// RequestScreenshot signature mirrors Runtime/Engine/Public/UnrealClient.h.
#pragma once

#include "Containers/UnrealString.h"

class FViewport
{
public:
    virtual ~FViewport() = default;
    // Runs one frame for this viewport on the calling (game) thread and services
    // any pending FScreenshotRequest before returning.
    void Draw(bool bShouldPresent = true);
};

class FScreenshotRequest
{
public:
    static void RequestScreenshot(const FString& InFilename, bool bInShowUI, bool bAddFilenameSuffix);
    static const FString& GetFilename();
    static bool IsScreenshotRequested();
};
