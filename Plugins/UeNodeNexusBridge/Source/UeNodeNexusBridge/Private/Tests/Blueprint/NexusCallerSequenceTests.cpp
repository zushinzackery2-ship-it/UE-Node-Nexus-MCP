#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/Patch/UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Transcode/Blueprint/Apply/UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscode.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCallerSequence,
    "Nexus.Stress.Blueprint.CallerSignatureSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCallerSequence::RunTest(const FString& Parameters)
{
    int32 Seed = 2711;
    int32 Steps = 32;
    FParse::Value(FCommandLine::Get(), TEXT("NexusStressSeed="), Seed);
    FParse::Value(FCommandLine::Get(), TEXT("NexusStressSteps="), Steps);
    Steps = FMath::Clamp(Steps, 1, 1000);
    FRandomStream Random(Seed);
    auto* Owner = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusCallerSequence")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    FApplyContext Context;
    Context.bDryRun = false;
    auto Apply = [&](const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Plan;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan))
        {
            AddError(TEXT("invalid test plan"));
            return false;
        }
        Context.Failures.Reset();
        ApplyBlueprintPlan(Owner, Plan, Context);
        for (const auto& Failure : Context.Failures)
        {
            AddError(Failure.Message);
        }
        return Context.Failures.IsEmpty();
    };
    if (!Apply(TEXT(R"([
        {"op":"bp_function_add","name":"Query","signature":{"inputs":[],"outputs":[],"flags":["Public"]}},
        {"op":"bp_function_add","name":"Caller","signature":{"inputs":[],"outputs":[],"flags":["Public"]}},
        {"op":"create_node","graph":"Caller","id":"call","class":"K2Node_CallFunction","positional":["self.Query"]}
    ])")))
    {
        return false;
    }
    auto* Graph = FindBlueprintGraph(Owner, TEXT("Caller"));
    auto* Call = Cast<UK2Node_CallFunction>(ResolveGraphNode(Owner, Graph, Context, TEXT("call")));
    if (!TestNotNull(TEXT("persistent call node"), Call))
    {
        return false;
    }
    const FGuid Guid = Call->NodeGuid;
    FString PreviousName;
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        const int32 Variant = Step < 4 ? Step : Random.RandRange(0, 3);
        const FString Name = Variant == 1 ? TEXT("Accepted") : TEXT("Valid");
        const FString Category = Variant == 2 ? TEXT("int") : TEXT("bool");
        const FString Outputs = Variant == 3 ? TEXT("[]") : FString::Printf(
            TEXT(R"([{"name":"%s","type":{"category":"%s"}}])"), *Name, *Category);
        const FString Plan = FString::Printf(TEXT(R"([
            {"op":"bp_function_signature_set","name":"Query","signature":{"inputs":[],"outputs":%s,"flags":["Public"]}},
            {"op":"refresh_function_call","graph":"Caller","id":"call"}
        ])"), *Outputs);
        AddInfo(FString::Printf(TEXT("NexusStress seed=%d step=%d variant=%d plan=%s"), Seed, Step, Variant, *Plan));
        if (!Apply(Plan))
        {
            return false;
        }
        TestEqual(TEXT("caller identity survives signature changes"), Call->NodeGuid, Guid);
        UEdGraphPin* Pin = Call->FindPin(*Name, EGPD_Output);
        if (Variant == 3)
        {
            TestNull(TEXT("removed output is absent"), Pin);
        }
        else if (TestNotNull(TEXT("current output exists"), Pin))
        {
            TestEqual(TEXT("current output has native type"), Pin->PinType.PinCategory, FName(*Category));
        }
        if (!PreviousName.IsEmpty() && PreviousName != Name)
        {
            TestNull(TEXT("renamed output is absent"), Call->FindPin(*PreviousName, EGPD_Output));
        }
        FCompilerResultsLog Compile;
        FKismetEditorUtilities::CompileBlueprint(Owner, EBlueprintCompileOptions::None, &Compile);
        TestEqual(TEXT("native compiler accepts the evolved graph"), Compile.NumErrors, 0);
        PreviousName = Variant == 3 ? FString() : Name;
    }
    return !HasAnyErrors();
}
#endif
