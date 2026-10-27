#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Material/Functions/NexusFunctionCallRebind.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialFunction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transcode/Material/UeNodeNexusBridgeTranscodeMaterialApply.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
template <typename T>
T* AddRebindExpression(UMaterialFunction* Function)
{
    return CastChecked<T>(UeNodeNexusBridge::CreateNexusMaterialExpression(Function, T::StaticClass(), 0, 0));
}

// A function with input "Tint" and one output per name, in that order, each fed by a constant.
TStrongObjectPtr<UMaterialFunction> MakeRebindFunction(const TCHAR* Name, TArrayView<const TCHAR* const> Outputs)
{
    TStrongObjectPtr<UMaterialFunction> Function(NewObject<UMaterialFunction>(GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterialFunction::StaticClass(), FName(Name))));
    AddRebindExpression<UMaterialExpressionFunctionInput>(Function.Get())->InputName = TEXT("Tint");
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        auto* Value = AddRebindExpression<UMaterialExpressionConstant>(Function.Get());
        Value->R = static_cast<float>(Index);
        auto* Output = AddRebindExpression<UMaterialExpressionFunctionOutput>(Function.Get());
        Output->OutputName = Outputs[Index];
        Output->SortPriority = Index;
        Output->A.Connect(0, Value);
    }
    return Function;
}

// Parent calls Old (outputs A, B, RawColor) and reads RawColor at index 2 and B at index 1;
// New only has RawColor (index 0) and Other, so RawColor must move and B must be cut.
struct FRebindGraph
{
    TStrongObjectPtr<UMaterialFunction> Old;
    TStrongObjectPtr<UMaterialFunction> New;
    TStrongObjectPtr<UMaterialFunction> Parent;
    UMaterialExpressionMaterialFunctionCall* Call = nullptr;
    UMaterialExpressionConstant* Tint = nullptr;
    UMaterialExpressionFunctionOutput* RawReader = nullptr;
    UMaterialExpressionMultiply* DroppedReader = nullptr;

    FRebindGraph()
    {
        const TCHAR* const OldOutputs[]
        {
            TEXT("A"), TEXT("B"), TEXT("RawColor")
        };
        const TCHAR* const NewOutputs[]
        {
            TEXT("RawColor"), TEXT("Other")
        };
        Old = MakeRebindFunction(TEXT("NexusRebindOld"), OldOutputs);
        New = MakeRebindFunction(TEXT("NexusRebindNew"), NewOutputs);
        Parent = TStrongObjectPtr<UMaterialFunction>(NewObject<UMaterialFunction>(GetTransientPackage()));
        Call = AddRebindExpression<UMaterialExpressionMaterialFunctionCall>(Parent.Get());
        Call->SetMaterialFunction(Old.Get());
        Tint = AddRebindExpression<UMaterialExpressionConstant>(Parent.Get());
        Call->FunctionInputs[0].Input.Connect(0, Tint);
        RawReader = AddRebindExpression<UMaterialExpressionFunctionOutput>(Parent.Get());
        RawReader->A.Connect(2, Call);
        DroppedReader = AddRebindExpression<UMaterialExpressionMultiply>(Parent.Get());
        DroppedReader->A.Connect(1, Call);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFunctionCallRebind,
    "Nexus.Issues21.Material.FunctionCallRebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFunctionCallRebind::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    // Each cut link and the refused rebind are logged exactly once.
    AddExpectedError(TEXT("new function has no output for"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("the call would be circular"), EAutomationExpectedErrorFlags::Contains, 1);
    FRebindGraph Graph;
    TestEqual(TEXT("call starts on the old interface"), Graph.Call->FunctionOutputs.Num(), 3);
    // While a function is edited its expressions point at the preview material, a graph
    // that holds none of these readers; the engine fixup ran over that graph instead.
    TStrongObjectPtr<UMaterial> Preview(NewObject<UMaterial>(GetTransientPackage()));
    Graph.Call->Material = Preview.Get();

    FFunctionCallRebind Result;
    FString Error;
    TestTrue(TEXT("rebind succeeds"), RebindFunctionCall(Graph.Call, Graph.New.Get(), Result, Error));
    TestTrue(TEXT("call names the new function"), Graph.Call->MaterialFunction == Graph.New.Get());
    TestEqual(TEXT("call reads the new interface"), Graph.Call->FunctionOutputs.Num(), 2);
    TestTrue(TEXT("RawColor reader keeps the call"), Graph.RawReader->A.Expression == Graph.Call);
    TestEqual(TEXT("RawColor reader follows its name to index 0"), Graph.RawReader->A.OutputIndex, 0);
    TestNull(TEXT("reader of an output the new function lacks is cut"), Graph.DroppedReader->A.Expression);
    TestEqual(TEXT("one link followed"), Result.Followed, 1);
    TestEqual(TEXT("one link dropped"), Result.Dropped.Num(), 1);
    TestTrue(TEXT("dropped link names its output"), Result.Dropped.Num() == 1 && Result.Dropped[0].EndsWith(TEXT("<- B")));
    TestTrue(TEXT("same-named input keeps its link"), Graph.Call->FunctionInputs.Num() == 1
        && Graph.Call->FunctionInputs[0].Input.Expression == Graph.Tint);
    TestTrue(TEXT("preview back-pointer survives the rebind"), Graph.Call->Material == Preview.Get());

    FFunctionCallRebind Refused;
    TestFalse(TEXT("a function cannot call itself"), RebindFunctionCall(Graph.Call, Graph.Parent.Get(), Refused, Error));
    TestTrue(TEXT("circular call is explained"), Error.Contains(TEXT("circular")));
    TestTrue(TEXT("refused rebind leaves the call"), Graph.Call->MaterialFunction == Graph.New.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFunctionCallRebindPlan,
    "Nexus.Issues21.Material.FunctionCallRebindPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFunctionCallRebindPlan::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace Transcode;
    // The dry run applies the plan to a preview copy, so each mode logs the cut link once.
    AddExpectedError(TEXT("new function has no output for"), EAutomationExpectedErrorFlags::Contains, 2);
    FRebindGraph Graph;
    const FString Text = FString::Printf(TEXT(R"([{"op":"set_node_param","id":"call","name":"MaterialFunction","value":"%s"}])"),
        *Graph.New->GetPathName());
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
    const bool DryRuns[]
    {
        true, false
    };
    for (bool bDryRun : DryRuns)
    {
        const FString Mode = bDryRun ? TEXT("dry run: ") : TEXT("apply: ");
        FApplyContext Context;
        Context.bDryRun = bDryRun;
        Context.Ids.Add(TEXT("call"), MaterialExpressionKey(Graph.Call, OwnerExpressions(Graph.Parent.Get())));
        ApplyMaterialPlan(Graph.Parent.Get(), Plan, Context);
        TestEqual(Mode + TEXT("plan applies"), Context.Failures.Num(), 0);
        TestTrue(Mode + TEXT("dropped link is a note"), Context.Notes.Num() == 1
            && Context.Notes[0].Code == TEXT("function_call_link_dropped"));
        TestTrue(Mode + TEXT("call names the expected function"),
            Graph.Call->MaterialFunction == (bDryRun ? Graph.Old.Get() : Graph.New.Get()));
        TestTrue(Mode + TEXT("RawColor reader keeps the call"), Graph.RawReader->A.Expression == Graph.Call);
        TestEqual(Mode + TEXT("RawColor reader index"), Graph.RawReader->A.OutputIndex, bDryRun ? 2 : 0);
    }
    return true;
}
#endif
