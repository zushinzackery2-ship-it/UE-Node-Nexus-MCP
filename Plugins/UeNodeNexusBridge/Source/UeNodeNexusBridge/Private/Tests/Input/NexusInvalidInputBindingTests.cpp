#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInvalidInputBinding,
    "Nexus.Issues7.Input.InvalidBindingsRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInvalidInputBinding::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("InvalidInputBinding")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    UEdGraph* Graph = Blueprint->UbergraphPages[0];
    const int32 OriginalNodes = Graph->Nodes.Num();
    for (bool bAccessor : { false, true })
    {
        for (const FString& Action : { FString(), Blueprint->GetPathName() })
        {
            auto Op = MakeShared<FJsonObject>();
            Op->SetStringField(TEXT("id"), TEXT("invalid"));
            Op->SetStringField(TEXT("class"), bAccessor ? TEXT("CallFunction") : TEXT("GetInputActionValue"));
            TArray<TSharedPtr<FJsonValue>> Positional;
            Positional.Add(MakeShared<FJsonValueString>(bAccessor
                ? FString(TEXT("/Script/EnhancedInput.EnhancedInputLibrary.GetBoundActionValue")) : Action));
            Op->SetArrayField(TEXT("positional"), Positional);
            if (bAccessor)
            {
                auto Args = MakeShared<FJsonObject>();
                Args->SetStringField(TEXT("Action"), Action);
                Op->SetObjectField(TEXT("params"), Args);
            }
            for (bool bPreview : { true, false })
            {
                Transcode::FApplyContext Context;
                Context.bDryRun = bPreview;
                Transcode::ApplyBlueprintCreateNode(Blueprint, Graph, Op, 0, Context);
                TestEqual(TEXT("missing and wrong-class actions are refused"), Context.Failures.Num(), 1);
                TestEqual(TEXT("invalid binding leaves the graph unchanged"), Graph->Nodes.Num(), OriginalNodes);
            }
        }
    }
    return true;
}
#endif
