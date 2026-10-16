#include "ConfigurationDiagnostics.h"

namespace NexusLifecycle::Validation
{
namespace
{
template <typename FileType>
int32 EntryCount(const FileType& File)
{
    int32 Count = 0;
    for (const auto& Section : File)
    {
        Count += Section.Value.Num();
    }
    return Count;
}

const FConfigSection* SectionFor(const FConfigFile& File, const FString& SectionName)
{
    return File.FindSection(SectionName);
}

const FConfigCommandStreamSection* SectionFor(const FConfigCommandStream& File, const FString& SectionName)
{
    return File.Find(SectionName);
}

template <typename FileType>
int32 KeyCount(const FileType& File, const FString& SectionName, FName Key)
{
    const auto* Section = SectionFor(File, SectionName);
    int32 Count = 0;
    if (Section != nullptr)
    {
        for (auto Entry = Section->CreateConstKeyIterator(Key); Entry; ++Entry)
        {
            ++Count;
        }
    }
    return Count;
}

int32 RestoredCount(const FConfigFile& Runtime, const FConfigFile& Effective)
{
    int32 Count = 0;
    for (const auto& Section : Effective)
    {
        for (const auto& Entry : Section.Value)
        {
            Count += KeyCount(Runtime, Section.Key, Entry.Key) == 0 ? 1 : 0;
        }
    }
    return Count;
}

void LogClassKeys(const FConfigFile& Runtime, const FConfigFile& Effective, const FConfigBranch& Branch)
{
    const FString SectionName = TEXT("/Script/Engine.Engine");
    TSet<FName> Keys;
    for (const FConfigFile* File : {&Runtime, &Branch.FinalCombinedLayers, &Effective})
    {
        if (const FConfigSection* Section = File->FindSection(SectionName))
        {
            for (const auto& Entry : *Section)
            {
                if (Entry.Key.ToString().Contains(TEXT("ClassName")))
                {
                    Keys.Add(Entry.Key);
                }
            }
        }
    }
    TArray<FName> Ordered = Keys.Array();
    Ordered.Sort(FNameLexicalLess());
    for (FName Key : Ordered)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus configuration class source key=%s runtime=%d final=%d saved=%d command=%d effective=%d"),
            *Key.ToString(), KeyCount(Runtime, SectionName, Key), KeyCount(Branch.FinalCombinedLayers, SectionName, Key),
            KeyCount(Branch.SavedLayer, SectionName, Key), KeyCount(Branch.CommandLineOverrides, SectionName, Key),
            KeyCount(Effective, SectionName, Key));
    }
}
}

void LogConfigurationLayers(const FString& Category, const FConfigFile& Runtime, const FConfigFile& Effective)
{
    const FConfigBranch* Branch = Runtime.Branch;
    if (Branch == nullptr)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus configuration layers category=%s branch=0 runtime=%d effective=%d restored=%d"),
            *Category, EntryCount(Runtime), EntryCount(Effective), RestoredCount(Runtime, Effective));
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus configuration layers category=%s branch=1 hierarchical=%d replay=%d runtime=%d final=%d saved=%d command=%d effective=%d restored=%d ini=%s engine=%s project=%s"),
        *Category, Branch->bIsHierarchical, static_cast<int32>(Branch->ReplayMethod), EntryCount(Runtime),
        EntryCount(Branch->FinalCombinedLayers), EntryCount(Branch->SavedLayer), EntryCount(Branch->CommandLineOverrides),
        EntryCount(Effective), RestoredCount(Runtime, Effective), *Branch->IniPath, *Branch->SourceEngineConfigDir,
        *Branch->SourceProjectConfigDir);
    if (Category == TEXT("Engine"))
    {
        LogClassKeys(Runtime, Effective, *Branch);
    }
}
}
