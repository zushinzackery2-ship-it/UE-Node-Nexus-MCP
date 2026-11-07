#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/Patch/UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNexusColdOracle,
    "Nexus.Stress.ColdReadback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FNexusColdOracle::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    FString File;
    if (FParse::Value(FCommandLine::Get(), TEXT("NexusStressOracle="), File))
    {
        Names.Add(TEXT("SavedCandidate"));
        Commands.Add(File);
    }
}

bool FNexusColdOracle::RunTest(const FString& Parameters)
{
    FString Text;
    TSharedPtr<FJsonObject> Oracle;
    if (!FFileHelper::LoadFileToString(Text, *Parameters)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Oracle))
    {
        AddError(TEXT("cold oracle file could not be read"));
        return false;
    }
    const FString Root = Oracle->GetStringField(TEXT("root"));
    const auto Action = Oracle->GetObjectField(TEXT("action"));
    auto* Material = LoadObject<UMaterial>(nullptr, *(Root + TEXT("/M_Sequence.M_Sequence")));
    auto* Provider = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("/BFL_Sequence.BFL_Sequence")));
    auto* Caller = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("/BP_Sequence.BP_Sequence")));
    if (!TestNotNull(TEXT("cold material"), Material) || !TestNotNull(TEXT("cold provider"), Provider)
        || !TestNotNull(TEXT("cold caller"), Caller))
    {
        return false;
    }
    TestEqual(TEXT("native material sidedness"), !!Material->TwoSided, Action->GetBoolField(TEXT("two_sided")));
    int32 Constants = 0;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (const auto* Constant = Cast<UMaterialExpressionConstant>(Expression))
        {
            ++Constants;
            TestEqual(TEXT("native constant value"), Constant->R, static_cast<float>(Action->GetNumberField(TEXT("value"))));
            const FExpressionInput* Input = Material->GetExpressionInputForProperty(MP_EmissiveColor);
            TestTrue(TEXT("authored constant drives the material output"), Input && Input->Expression == Constant);
        }
    }
    TestEqual(TEXT("one authored constant"), Constants, 1);
    if (!TestNotNull(TEXT("cold provider generated class"), Provider->GeneratedClass.Get())
        || !TestNotNull(TEXT("cold caller generated class"), Caller->GeneratedClass.Get()))
    {
        return false;
    }
    TestTrue(TEXT("cold provider compiled"), Provider->Status != BS_Error);
    TestTrue(TEXT("cold caller compiled"), Caller->Status != BS_Error);
    UFunction* Query = Provider->GeneratedClass->FindFunctionByName(TEXT("Query"));
    UFunction* Exercise = Caller->GeneratedClass->FindFunctionByName(TEXT("Exercise"));
    if (!TestNotNull(TEXT("native Query function"), Query) || !TestNotNull(TEXT("native Exercise function"), Exercise))
    {
        return false;
    }
    FProperty* Output = FindFProperty<FProperty>(Query, *Action->GetStringField(TEXT("output")));
    FProperty* Result = FindFProperty<FProperty>(Exercise, TEXT("Value"));
    if (!TestNotNull(TEXT("current native output"), Output) || !TestNotNull(TEXT("native caller result"), Result))
    {
        return false;
    }
    const bool bBool = Action->GetStringField(TEXT("output_type")) == TEXT("bool");
    TestTrue(TEXT("native output has the authored type"), bBool ? CastField<FBoolProperty>(Output) != nullptr : CastField<FIntProperty>(Output) != nullptr);
    FStructOnScope Arguments(Exercise);
    Caller->GeneratedClass->GetDefaultObject()->ProcessEvent(Exercise, Arguments.GetStructMemory());
    if (bBool && CastField<FBoolProperty>(Result))
    {
        TestTrue(TEXT("linked Blueprint call executes non-default bool return"),
            CastFieldChecked<FBoolProperty>(Result)->GetPropertyValue_InContainer(Arguments.GetStructMemory()));
    }
    else if (!bBool && CastField<FIntProperty>(Result))
    {
        TestEqual(TEXT("linked Blueprint call executes non-default integer return"),
            CastFieldChecked<FIntProperty>(Result)->GetPropertyValue_InContainer(Arguments.GetStructMemory()), 37);
    }
    else
    {
        AddError(TEXT("caller output has wrong native type"));
    }
    return !HasAnyErrors();
}
#endif
