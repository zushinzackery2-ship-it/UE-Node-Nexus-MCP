#pragma once

#include "CoreMinimal.h"

namespace UeNodeNexusBridge::Transcode
{
struct FPackageFileState
{
    uint64 Modified = 0;
    int64 Size = 0;

    bool operator==(const FPackageFileState& Other) const
    {
        return Modified == Other.Modified && Size == Other.Size;
    }
};

bool ReadPackageFileState(const FString& Filename, FPackageFileState& OutState);
}
