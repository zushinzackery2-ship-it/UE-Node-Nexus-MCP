// FViewport / FScreenshotRequest stubs for compiler-only checks. The
// RequestScreenshot signature mirrors Runtime/Engine/Public/UnrealClient.h.
#pragma once

#include "Containers/UnrealString.h"

class FViewport
{
public:
    virtual ~FViewport() = default;
};

class FScreenshotRequest
{
public:
    static void RequestScreenshot(const FString& InFilename, bool bInShowUI, bool bAddFilenameSuffix);
    static const FString& GetFilename();
    static bool IsScreenshotRequested();
};
