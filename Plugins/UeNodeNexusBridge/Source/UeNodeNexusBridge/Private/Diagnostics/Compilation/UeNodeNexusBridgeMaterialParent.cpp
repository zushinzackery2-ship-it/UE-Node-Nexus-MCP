#include "UeNodeNexusBridgeCompilation.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

namespace UeNodeNexusBridge
{
bool EnsureMaterialParentReady(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Plan, bool bWait, FString& Error)
{
    UMaterialInstance* Instance = Cast<UMaterialInstance>(Asset);
    UMaterialInterface* Parent = Instance ? Instance->Parent : nullptr;
    for (const auto& Value : Plan)
    {
        const TSharedPtr<FJsonObject>* Op = nullptr;
        FString Verb, Name, Text;
        if (Value.IsValid() && Value->TryGetObject(Op) && (*Op)->TryGetStringField(TEXT("op"), Verb)
            && Verb == TEXT("set_asset_prop") && (*Op)->TryGetStringField(TEXT("name"), Name) && Name == TEXT("Parent"))
        {
            if (!(*Op)->TryGetStringField(TEXT("value"), Text))
            {
                Error = TEXT("material_parent_required");
                return false;
            }
            Parent = LoadObject<UMaterialInterface>(nullptr, *FPackageName::ExportTextPathToObjectPath(Text));
        }
    }
    if (!Parent)
    {
        Error = TEXT("material_parent_required");
        return false;
    }
    TSet<UMaterialInterface*> Seen;
    UMaterialInterface* Current = Parent;
    while (Current)
    {
        if (Current == Instance || Seen.Contains(Current))
        {
            Error = TEXT("material_parent_cycle");
            return false;
        }
        Seen.Add(Current);
        UMaterialInstance* Ancestor = Cast<UMaterialInstance>(Current);
        Current = Ancestor ? Ancestor->Parent : nullptr;
    }
    const FBridgeAssetCompileDiagnostics Status = MaterialResourceStatus(Parent, bWait);
    if (!Status.bOk)
    {
        Error = TEXT("parent_material_not_ready: ") + Parent->GetPathName() + TEXT(" state=") + Status.State;
        return false;
    }
    return true;
}
}
