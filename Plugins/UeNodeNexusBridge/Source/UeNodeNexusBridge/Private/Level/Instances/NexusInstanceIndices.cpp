#include "NexusInstanceIndices.h"

namespace UeNodeNexusBridge::Instances
{
bool ApplyIndexUpdates(TArray<FGuid>& Ids,
    TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> Updates, int32 NewCount)
{
    using EUpdate = FInstancedStaticMeshDelegates::EInstanceIndexUpdateType;
    TArray<TPair<int32, FGuid>> Moves;
    for (const auto& Update : Updates)
    {
        if (Update.Type == EUpdate::Destroyed)
        {
            return true;
        }
        if (Update.Type == EUpdate::Cleared)
        {
            Ids.Reset();
        }
        if (Update.Type == EUpdate::Relocated)
        {
            if (!Ids.IsValidIndex(Update.OldIndex) || Update.Index < 0 || Update.Index >= NewCount)
            {
                return false;
            }
            Moves.Emplace(Update.Index, Ids[Update.OldIndex]);
        }
    }
    Ids.SetNum(FMath::Max(Ids.Num(), NewCount), EAllowShrinking::No);
    for (const auto& Update : Updates)
    {
        if (Update.Type == EUpdate::Added || Update.Type == EUpdate::Removed)
        {
            if (!Ids.IsValidIndex(Update.Index))
            {
                return false;
            }
            Ids[Update.Index] = Update.Type == EUpdate::Added ? FGuid::NewGuid() : FGuid();
        }
    }
    for (const auto& Move : Moves)
    {
        Ids[Move.Key] = Move.Value;
    }
    Ids.SetNum(NewCount, EAllowShrinking::No);
    return true;
}
}
