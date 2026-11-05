#include "NexusAssetDeleteOrder.h"

#include "AssetRegistry/AssetRegistryModule.h"

namespace UeNodeNexusBridge::AssetDeletion
{
// Dependents come first, so the recorded order states which member referenced
// which. The deletion itself is one call over the whole set.
TArray<int32> DependentsFirst(const TArray<FClosureMember>& Members)
{
    const int32 Count = Members.Num();
    TMap<FName, int32> Index;
    for (int32 Member = 0; Member < Count; ++Member)
    {
        Index.Add(Members[Member].PackageName, Member);
    }
    TArray<TArray<int32>> Dependencies;
    Dependencies.SetNum(Count);
    TArray<int32> Dependents;
    Dependents.Init(0, Count);
    for (int32 Member = 0; Member < Count; ++Member)
    {
        TArray<FName> Names;
        FAssetRegistryModule::GetRegistry().GetDependencies(Members[Member].PackageName, Names);
        for (const FName Name : Names)
        {
            const int32* Dependency = Index.Find(Name);
            if (Dependency != nullptr && *Dependency != Member && !Dependencies[Member].Contains(*Dependency))
            {
                Dependencies[Member].Add(*Dependency);
                ++Dependents[*Dependency];
            }
        }
    }
    TArray<int32> Order;
    TArray<bool> Placed;
    Placed.Init(false, Count);
    for (bool bProgress = true; bProgress && Order.Num() < Count;)
    {
        bProgress = false;
        for (int32 Member = 0; Member < Count; ++Member)
        {
            if (Placed[Member] || Dependents[Member] != 0)
            {
                continue;
            }
            Placed[Member] = true;
            Order.Add(Member);
            bProgress = true;
            for (const int32 Dependency : Dependencies[Member])
            {
                --Dependents[Dependency];
            }
        }
    }
    // Members of a reference cycle have no dependents-first order; they keep request order.
    for (int32 Member = 0; Member < Count; ++Member)
    {
        if (!Placed[Member])
        {
            Order.Add(Member);
        }
    }
    return Order;
}

}
