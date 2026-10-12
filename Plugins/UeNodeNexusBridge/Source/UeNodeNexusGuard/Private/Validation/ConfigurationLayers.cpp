#include "ConfigurationLayers.h"

namespace NexusLifecycle::Validation
{
FConfigFile EffectiveConfigurationFile(const FConfigFile& Runtime)
{
    const FConfigBranch* Branch = Runtime.Branch;
    FConfigFile Effective = Runtime;
    Effective.Branch = nullptr;
    if (Branch != nullptr && Branch->bIsHierarchical)
    {
        // ResetKeyInSection removes inherited defaults from the live cache and
        // its saved change stream. Replay recovers inherited defaults while
        // retaining explicit clear/remove commands and command-line overrides.
        Effective = Branch->FinalCombinedLayers;
        Effective.Branch = nullptr;
        Effective.ApplyFile(&Branch->SavedLayer);
        Effective.ApplyFile(&Branch->CommandLineOverrides);

        for (const auto& Section : Runtime)
        {
            FConfigSection Merged;
            TSet<FName> Replaced;
            for (const auto& Entry : Section.Value)
            {
                Replaced.Add(Entry.Key);
            }
            if (const FConfigSection* Inherited = Effective.FindSection(Section.Key))
            {
                for (const auto& Entry : *Inherited)
                {
                    if (!Replaced.Contains(Entry.Key))
                    {
                        Merged.Add(Entry.Key, Entry.Value);
                    }
                }
            }
            for (const auto& Entry : Section.Value)
            {
                Merged.Add(Entry.Key, Entry.Value);
            }
            Effective.Add(Section.Key, MoveTemp(Merged));
        }
    }
    return Effective;
}
}
