#include "ConfigurationLayers.h"

namespace NexusLifecycle::Validation
{
namespace
{
FConfigSection MergeSection(const FConfigSection* Inherited, const FConfigSection& Runtime)
{
    TSet<FName> Replaced;
    for (const auto& Entry : Runtime)
    {
        Replaced.Add(Entry.Key);
    }
    FConfigSection Merged;
    if (Inherited != nullptr)
    {
        for (const auto& Entry : *Inherited)
        {
            if (!Replaced.Contains(Entry.Key))
            {
                Merged.Add(Entry.Key, Entry.Value);
            }
        }
    }
    for (const auto& Entry : Runtime)
    {
        Merged.Add(Entry.Key, Entry.Value);
    }
    return Merged;
}
}

void ApplyConfigurationFile(FConfigFile& Runtime, const FConfigFile& Snapshot, EConfigurationPhase Phase)
{
    if (Phase == EConfigurationPhase::Runtime)
    {
        // A measured live snapshot is the authoritative reconstruction base.
        // Worker-local saved and launch commands have already been folded into
        // the source snapshot and must not reintroduce absent values or arrays.
        if (FConfigBranch* Branch = Runtime.Branch)
        {
            Branch->FinalCombinedLayers = Snapshot;
            Branch->FinalCombinedLayers.Branch = nullptr;
            Branch->SavedLayer.Empty();
            Branch->CommandLineOverrides.Empty();
        }
        Runtime.Empty();
    }
    // During PostConfigInit, sections absent from a live source may contain
    // startup inputs that UE consumes and removes from every branch layer.
    // Leave them available to their native consumers until initialization
    // completes; represented source sections still replace worker defaults.
    for (const auto& Section : Snapshot)
    {
        Runtime.Add(Section.Key, Section.Value);
    }
}

FConfigFile EffectiveConfigurationFile(const FConfigFile& Runtime)
{
    const FConfigBranch* Branch = Runtime.Branch;
    FConfigFile Effective = Runtime;
    Effective.Branch = nullptr;
    if (Branch != nullptr && Branch->FinalCombinedLayers.Num() > 0)
    {
        // UE 5.5 pre-creates known ini branches before hierarchical loading;
        // their bIsHierarchical flag is not a reliable description of the
        // populated layers. The loaded baseline itself owns this contract.
        // ResetKeyInSection removes inherited defaults from the live cache and
        // its saved change stream. Replay recovers inherited defaults while
        // retaining explicit clear/remove commands and command-line overrides.
        Effective = Branch->FinalCombinedLayers;
        Effective.Branch = nullptr;
        Effective.ApplyFile(&Branch->SavedLayer);
        Effective.ApplyFile(&Branch->CommandLineOverrides);

        for (const auto& Section : Runtime)
        {
            Effective.Add(Section.Key, MergeSection(Effective.FindSection(Section.Key), Section.Value));
        }
    }
    return Effective;
}
}
