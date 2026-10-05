#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "NiagaraParameterStore.h"
#include "Niagara/Transcode/Values/Storage/NexusNiagaraStorage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraStoragePerformance,
    "Nexus.Niagara.StoragePerformance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraStoragePerformance::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    FNiagaraParameterStore Store;
    const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("User.Quaternion"));
    TestTrue(TEXT("aligned reference parameter"), Store.SetParameterValue(FQuat4f::Identity, Variable, true));
    constexpr int32 Iterations = 200000;
    double Reference = 0;
    const double BeforeReference = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < Iterations; ++Index)
    {
        FQuat4f Value;
        Store.CopyParameterData(Variable, reinterpret_cast<uint8*>(&Value));
        Reference += Value.W;
    }
    const double ReferenceSeconds = FPlatformTime::Seconds() - BeforeReference;
    FString Error;
    double Verified = 0;
    const double BeforeVerified = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < Iterations; ++Index)
    {
        FQuat4f Value;
        if (!Storage::ReadBytes(Store, Variable, &Value, sizeof(Value), Error))
        {
            AddError(Error);
            return false;
        }
        Verified += Value.W;
    }
    const double VerifiedSeconds = FPlatformTime::Seconds() - BeforeVerified;
    TestEqual(TEXT("all guarded reads preserve the result"), Verified, Reference);
    UE_LOG(LogTemp, Display, TEXT("Nexus storage benchmark iterations=%d engine_copy_ns=%.3f guarded_copy_ns=%.3f"),
        Iterations, ReferenceSeconds * 1e9 / Iterations, VerifiedSeconds * 1e9 / Iterations);
    return true;
}

#endif
