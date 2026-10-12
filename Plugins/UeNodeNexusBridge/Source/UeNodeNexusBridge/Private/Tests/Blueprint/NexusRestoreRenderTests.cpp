#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/App.h"
#include "Transcode/Commit/Restore/NexusRestoreRenderScope.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRestoreRenderLifetime,
    "Nexus.Safety.RestoreRenderTargetLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRestoreRenderLifetime::RunTest(const FString& Parameters)
{
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
    Target->InitAutoFormat(16, 16);
    TWeakObjectPtr<UTextureRenderTarget2D> Weak = Target;
    {
        UeNodeNexusBridge::Collaboration::FRestoreRenderScope Scope;
        TestTrue(TEXT("reload holds transient render targets"), Scope.Num() > 0);
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        if (!TestTrue(TEXT("target survives the reload's garbage collection"), Weak.IsValid()))
        {
            return false;
        }
        if (FApp::CanEverRender())
        {
            TestNotNull(TEXT("render resource remains available"), Weak->GameThread_GetRenderTargetResource());
        }
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("reload protection does not retain unused textures permanently"), Weak.IsValid());
    return true;
}
#endif
