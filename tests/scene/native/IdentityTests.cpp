#include "Level/Instances/NexusInstanceData.h"
#include "Level/Instances/NexusInstanceIndices.h"

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
using EUpdate = FInstancedStaticMeshDelegates::EInstanceIndexUpdateType;
using FUpdate = FInstancedStaticMeshDelegates::FInstanceIndexUpdateData;

FUpdate Change(EUpdate Type, int32 Index = INDEX_NONE, int32 OldIndex = INDEX_NONE)
{
    FUpdate Result;
    Result.Type = Type;
    Result.Index = Index;
    Result.OldIndex = OldIndex;
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSwapIdentityTest, "Nexus.Instances.RemoveSwapIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSwapIdentityTest::RunTest(const FString&)
{
    TArray<FGuid> Ids;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Ids.Add(FGuid::NewGuid());
    }
    const FGuid First = Ids[0];
    const FGuid Last = Ids[3];
    TArray<FUpdate> Updates;
    Updates.Add(Change(EUpdate::Removed, 1));
    Updates.Add(Change(EUpdate::Relocated, 1, 3));
    TestTrue(TEXT("valid relocation"), UeNodeNexusBridge::Instances::ApplyIndexUpdates(Ids, Updates, 3));
    TestEqual(TEXT("unchanged identity"), Ids[0], First);
    TestEqual(TEXT("last instance retains identity"), Ids[1], Last);
    TestEqual(TEXT("new size"), Ids.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusShiftIdentityTest, "Nexus.Instances.RemoveShiftIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusShiftIdentityTest::RunTest(const FString&)
{
    TArray<FGuid> Ids;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Ids.Add(FGuid::NewGuid());
    }
    const FGuid Third = Ids[2];
    const FGuid Fourth = Ids[3];
    TArray<FUpdate> Updates;
    Updates.Add(Change(EUpdate::Removed, 1));
    Updates.Add(Change(EUpdate::Relocated, 1, 2));
    Updates.Add(Change(EUpdate::Relocated, 2, 3));
    TestTrue(TEXT("shift is valid"), UeNodeNexusBridge::Instances::ApplyIndexUpdates(Ids, Updates, 3));
    TestEqual(TEXT("overlapping relocation reads old positions"), Ids[1], Third);
    TestEqual(TEXT("second move preserves source"), Ids[2], Fourth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInvalidIdentityTest, "Nexus.Instances.InvalidRelocationRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInvalidIdentityTest::RunTest(const FString&)
{
    TArray<FGuid> Ids;
    Ids.Add(FGuid::NewGuid());
    TArray<FUpdate> Updates;
    Updates.Add(Change(EUpdate::Relocated, 0, 9));
    TestFalse(TEXT("unknown old identity must be rejected"), UeNodeNexusBridge::Instances::ApplyIndexUpdates(Ids, Updates, 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIdentityUndoTest, "Nexus.Instances.MetadataUndoRedo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusIdentityUndoTest::RunTest(const FString&)
{
    if (!GEditor || GEditor->IsTransactionActive())
    {
        AddError(TEXT("Run in an idle isolated editor test instance"));
        return false;
    }
    TStrongObjectPtr<UNexusInstanceData> Data(NewObject<UNexusInstanceData>(GetTransientPackage(), NAME_None, RF_Transactional));
    const FGuid Original = FGuid::NewGuid();
    Data->Ids.Add(Original);
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Nexus identity regression")));
        Data->Modify();
        Data->Ids.Add(FGuid::NewGuid());
        Data->bIdentityValid = false;
    }
    if (!TestTrue(TEXT("undo completed"), GEditor->UndoTransaction())
        || !TestEqual(TEXT("undo restores count"), Data->Ids.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("undo restores identity"), Data->Ids[0], Original);
    TestTrue(TEXT("undo restores validity"), Data->bIdentityValid);
    TestTrue(TEXT("redo completed"), GEditor->RedoTransaction());
    TestEqual(TEXT("redo restores count"), Data->Ids.Num(), 2);
    TestFalse(TEXT("redo restores conflict state"), Data->bIdentityValid);
    TestTrue(TEXT("identity metadata is excluded from cooked objects"), Data->IsEditorOnly());
    return true;
}

#endif
