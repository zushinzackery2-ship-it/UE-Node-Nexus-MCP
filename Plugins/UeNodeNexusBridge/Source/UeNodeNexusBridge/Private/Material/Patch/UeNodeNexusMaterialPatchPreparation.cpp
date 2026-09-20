#include "UeNodeNexusMaterialPatchPreparation.h"
#include "UeNodeNexusBridgeJson.h"
#include "NodeInterface/UeNodeNexusBridgeMaterialNodeInterfaceShared.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UeNodeNexusBridge
{
UObject* MakeMaterialPatchPreview(UObject* Asset)
{
    UObject* Copy = DuplicateObject(Asset, GetTransientPackage());
    if (Copy)
    {
        TArray<UObject*> Objects;
        GetObjectsWithOuter(Copy, Objects, true);
        Objects.Add(Copy);
        for (UObject* Object : Objects)
        {
            Object->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
            Object->SetFlags(RF_Transient);
        }
    }
    return Copy;
}

bool ValidateMaterialPatchAliases(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Operations,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    TSet<FString> All, Seen;
    auto Fail = [&](const FString& Code, const FString& Message)
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), Code, Message, Asset->GetPathName(), TEXT("UeNodeNexusBridge"))));
        return false;
    };
    for (const auto& Value : Operations)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
        {
            return Fail(TEXT("invalid_patch_operation"), TEXT("every operation must be an object"));
        }
        const auto Op = Value->AsObject();
        FString Verb, Id;
        Op->TryGetStringField(TEXT("op"), Verb);
        if (Verb == TEXT("create_node") && Op->HasField(TEXT("client_id")))
        {
            if (!Op->TryGetStringField(TEXT("client_id"), Id) || Id.IsEmpty() || All.Contains(Id)
                || (Cast<UMaterial>(Asset) ? ResolveMaterialInterfaceNode(Cast<UMaterial>(Asset), Id)
                    : ResolveMaterialInterfaceNode(Cast<UMaterialFunction>(Asset), Id)) || IsMaterialOutputNodeId(Id)
                || Id.Equals(TEXT("output"), ESearchCase::IgnoreCase) || Id.Equals(TEXT("MaterialOutput"), ESearchCase::IgnoreCase))
            {
                return Fail(TEXT("invalid_client_id"), TEXT("client_id must be unique and must not shadow an existing node: ") + Id);
            }
            All.Add(Id);
        }
    }
    for (const auto& Value : Operations)
    {
        const auto Op = Value->AsObject();
        FString Verb, Id;
        Op->TryGetStringField(TEXT("op"), Verb);
        for (const TCHAR* Key : { TEXT("node_id"), TEXT("node"), TEXT("from_node_id"), TEXT("from_node"), TEXT("to_node_id"), TEXT("to_node") })
        {
            FString Ref;
            if (Op->TryGetStringField(Key, Ref) && All.Contains(Ref) && !Seen.Contains(Ref))
            {
                return Fail(TEXT("forward_client_reference"), TEXT("create_node must precede references to client_id: ") + Ref);
            }
        }
        if (Verb == TEXT("create_node") && Op->TryGetStringField(TEXT("client_id"), Id))
        {
            Seen.Add(Id);
        }
    }
    return true;
}

bool MaterialPatchHasChanges(const TSharedPtr<FJsonObject>& Diff)
{
    for (const auto& Pair : Diff->Values)
    {
        if (Pair.Value->Type == EJson::Array && !Pair.Value->AsArray().IsEmpty())
        {
            return true;
        }
    }
    return false;
}
}
