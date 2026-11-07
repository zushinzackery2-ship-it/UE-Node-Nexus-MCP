#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Fixtures/NexusDiskAssets.h"
#include "Assets/Management/Move/NexusMovePreflight.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "EdGraphSchema_K2.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMoveDependencyBatch,
    "Nexus.Assets.MoveBatch.ColdDependencyClosure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMoveDependencyBatch::RunTest(const FString& Parameters)
{
    NexusTests::FDiskAssets Fixture(TEXT("NexusMoveBatch"));
    auto* Parent = Fixture.Blueprint(TEXT("Old/Parent"), AActor::StaticClass());
    auto* Child = Fixture.Blueprint(TEXT("Old/Child"), Parent->GeneratedClass);
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
    Type.PinSubCategoryObject = AActor::StaticClass();
    FBlueprintEditorUtils::AddMemberVariable(Child, TEXT("SoftParent"), Type);
    FKismetEditorUtilities::CompileBlueprint(Child);
    auto* Property = FindFProperty<FSoftClassProperty>(Child->GeneratedClass, TEXT("SoftParent"));
    if (!TestNotNull(TEXT("soft class fixture property"), Property))
    {
        Fixture.Cleanup();
        return false;
    }
    Property->SetPropertyValue_InContainer(Child->GeneratedClass->GetDefaultObject(), FSoftObjectPtr(Parent->GeneratedClass.Get()));
    FString Error;
    if (!TestTrue(TEXT("parent fixture saves"), Fixture.Save(Parent->GetOutermost(), Error))
        || !TestTrue(TEXT("child fixture saves"), Fixture.Save(Child->GetOutermost(), Error)))
    {
        AddError(Error);
        Fixture.Cleanup();
        return false;
    }
    TArray<TSharedPtr<FJsonValue>> Items;
    // Deliver the fixture's create/save notifications before renaming in the
    // same Automation tick; otherwise delayed Added events follow deletion.
    FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get()->Tick(0.0f);
    FAssetRegistryModule::GetRegistry().Tick(-1.0f);
    for (auto* Asset : {Child, Parent})
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("source_asset_path"), Asset->GetPathName());
        Item->SetStringField(TEXT("destination_asset_path"), Fixture.Package(*FString::Printf(TEXT("New/%s.%s"), *Asset->GetName(), *Asset->GetName())));
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetArrayField(TEXT("items"), Items);
    Payload->SetBoolField(TEXT("dry_run"), false);
    Payload->SetBoolField(TEXT("save"), true);
    Payload->SetBoolField(TEXT("fix_redirectors"), true);
    const auto Response = UeNodeNexusBridge::HandleAssetMoveBatch(TEXT("asset_move_batch"), TEXT("move-dependency"), Payload);
    if (!TestTrue(TEXT("complete dependency batch moves"), Response->GetBoolField(TEXT("ok"))))
    {
        FString Text;
        FJsonSerializer::Serialize(Response.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        AddError(Text);
        Fixture.Cleanup();
        return false;
    }
    const FString ParentPackage = Fixture.Package(TEXT("New/Parent"));
    const FString ChildPackage = Fixture.Package(TEXT("New/Child"));
    Parent = nullptr;
    Child = nullptr;
    Fixture.Unload({ParentPackage, ChildPackage});
    Parent = LoadObject<UBlueprint>(nullptr, *(ParentPackage + TEXT(".Parent")));
    Child = LoadObject<UBlueprint>(nullptr, *(ChildPackage + TEXT(".Child")));
    if (TestNotNull(TEXT("cold parent loads"), Parent) && TestNotNull(TEXT("cold child loads"), Child))
    {
        TestEqual(TEXT("cold hard dependency points to moved class"), Child->ParentClass.Get(), Parent->GeneratedClass.Get());
        Property = FindFProperty<FSoftClassProperty>(Child->GeneratedClass, TEXT("SoftParent"));
        if (TestNotNull(TEXT("cold soft class property"), Property))
        {
            TestEqual(TEXT("cold soft dependency points to moved class"),
                Property->GetPropertyValue_InContainer(Child->GeneratedClass->GetDefaultObject()).ToSoftObjectPath().ToString(),
                Parent->GeneratedClass->GetPathName());
        }
    }
    TestFalse(TEXT("old child package retired after referencers saved"), FPackageName::DoesPackageExist(Fixture.Package(TEXT("Old/Child"))));
    TestFalse(TEXT("old parent package retired after referencers saved"), FPackageName::DoesPackageExist(Fixture.Package(TEXT("Old/Parent"))));
    Fixture.Cleanup();
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMoveCdoSoftReferences,
    "Nexus.Assets.MoveBatch.UnloadedNativeCdoSoftReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMoveCdoSoftReferences::RunTest(const FString& Parameters)
{
    auto* Defaults = GetMutableDefault<AWorldSettings>();
    const auto Previous = Defaults->OverrideBaseMaterial;
    ON_SCOPE_EXIT { Defaults->OverrideBaseMaterial = Previous; };
    const FSoftObjectPath Source(TEXT("/Game/NexusUnloadedCdoMaterial.NexusUnloadedCdoMaterial"));
    Defaults->OverrideBaseMaterial = TSoftObjectPtr<UMaterialInterface>(Source);
    TMap<FSoftObjectPath, FSoftObjectPath> Redirects;
    Redirects.Add(Source, FSoftObjectPath(TEXT("/Game/New/NexusMaterial.NexusMaterial")));
    const auto References = UeNodeNexusBridge::AssetMove::CdoReferences(Redirects);
    TestTrue(TEXT("unloaded soft pointer is detected before native modal rename"),
        References.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Item)
        {
            const auto Row = Item->AsObject();
            return Row->GetStringField(TEXT("cdo")) == Defaults->GetPathName()
                && Row->GetStringField(TEXT("asset")) == Source.ToString();
        }));
    return !HasAnyErrors();
}
#endif
