#include "NexusSchema.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge::Transcode
{
bool LoadSchemaBlueprintClasses(FString& Error)
{
    const double Started = FPlatformTime::Seconds();
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    Registry.WaitForCompletion();
    TArray<FTopLevelAssetPath> Bases;
    Bases.Add(AActor::StaticClass()->GetClassPathName());
    Bases.Add(UActorComponent::StaticClass()->GetClassPathName());
    TSet<FTopLevelAssetPath> Derived;
    Registry.GetDerivedClassNames(Bases, TSet<FTopLevelAssetPath>(), Derived);
    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;
    Filter.bIncludeOnlyOnDiskAssets = true;
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);
    int32 Loaded = 0;
    for (const FAssetData& Asset : Assets)
    {
        FString ExportPath;
        if (!Asset.GetTagValue(FName(TEXT("GeneratedClass")), ExportPath))
        {
            continue;
        }
        const FString Path = FPackageName::ExportTextPathToObjectPath(ExportPath);
        if (!Derived.Contains(FSoftObjectPath(Path).GetAssetPath()))
        {
            continue;
        }
        if (FindObject<UClass>(nullptr, *Path) != nullptr)
        {
            continue;
        }
        if (LoadObject<UClass>(nullptr, *Path) == nullptr)
        {
            Error = TEXT("cannot reflect registered Blueprint class: ") + Path;
            return false;
        }
        ++Loaded;
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus phase=schema_blueprint_classes registered=%d loaded=%d duration_ms=%.3f"),
        Derived.Num(), Loaded, (FPlatformTime::Seconds() - Started) * 1000.0);
    return true;
}
}
