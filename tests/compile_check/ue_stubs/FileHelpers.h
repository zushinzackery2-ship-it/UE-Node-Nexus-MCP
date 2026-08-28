// FEditorFileUtils stub for compiler-only checks. LoadMap mirrors the
// documented UE 5.5 signature in Editor/UnrealEd/Public/FileHelpers.h.
#pragma once

#include "Containers/UnrealString.h"

class FEditorFileUtils
{
public:
    static bool LoadMap(const FString& Filename, bool LoadAsTemplate = false, const bool bShowProgress = true);
};
