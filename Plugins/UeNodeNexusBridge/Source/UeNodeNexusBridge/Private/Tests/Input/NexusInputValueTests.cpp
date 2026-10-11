#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/Input/NexusEnhancedInputValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputLibrary.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInputValueBinding,
    "Nexus.Issues7.Input.CurrentValueCompileSaveReadback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInputValueBinding::RunTest(const FString& Parameters)
{
    const FString Prefix = TEXT("/Game/NexusInput_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* ActionPackage = CreatePackage(*(Prefix + FString::FromInt(Index) + TEXT("Action")));
        auto* Action = NewObject<UInputAction>(ActionPackage, TEXT("Action"), RF_Public | RF_Standalone);
        Action->ValueType = static_cast<EInputActionValueType>(Index);
        auto* Package = CreatePackage(*(Prefix + FString::FromInt(Index) + TEXT("Blueprint")));
        auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package,
            TEXT("Blueprint"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        const FString BlueprintPath = Blueprint->GetPathName();
        const FString ActionPath = Action->GetPathName();
        ON_SCOPE_EXIT
        {
            for (const FString& Path : { BlueprintPath, ActionPath })
            {
                if (auto* Object = FindObject<UObject>(nullptr, *Path))
                {
                    auto* Owner = Object->GetOutermost();
                    ResetLoaders(Owner);
                    IFileManager::Get().Delete(*Collaboration::PackageFile(Owner));
                    Owner->SetDirtyFlag(false);
                    Object->ClearFlags(RF_Public | RF_Standalone);
                }
            }
        };
        UEdGraph* Graph = Blueprint->UbergraphPages[0];
        for (bool bAccessor : { false, true })
        {
            auto Op = MakeShared<FJsonObject>();
            Op->SetStringField(TEXT("id"), bAccessor ? TEXT("accessor") : TEXT("value"));
            Op->SetStringField(TEXT("class"), bAccessor ? TEXT("CallFunction") : TEXT("GetInputActionValue"));
            Op->SetArrayField(TEXT("positional"), { MakeShared<FJsonValueString>(bAccessor
                ? FString(TEXT("/Script/EnhancedInput.EnhancedInputLibrary.GetBoundActionValue")) : ActionPath) });
            if (bAccessor)
            {
                auto Args = MakeShared<FJsonObject>();
                Args->SetStringField(TEXT("Action"), ActionPath);
                Op->SetObjectField(TEXT("params"), Args);
            }
            Transcode::FApplyContext Preview;
            Preview.bDryRun = true;
            Transcode::ApplyBlueprintCreateNode(Blueprint, Graph, Op, 0, Preview);
            TestEqual(TEXT("creation preview accepts binding"), Preview.Failures.Num(), 0);
            Transcode::FApplyContext Context;
            Transcode::ApplyBlueprintCreateNode(Blueprint, Graph, Op, 0, Context);
            if (!TestEqual(TEXT("real text create path binds action before pins"), Context.Failures.Num(), 0))
            {
                AddError(Context.Failures[0].Message);
                return false;
            }
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        TestTrue(TEXT("current-value Blueprint compiles"), Blueprint->Status != BS_Error);
        FString Error;
        if (!TestTrue(TEXT("save action"), Transcode::SavePackageTo(ActionPackage, Action,
            Collaboration::PackageFile(ActionPackage), false, Error))
            || !TestTrue(TEXT("save compiled Blueprint"), Transcode::SavePackageTo(Package, Blueprint,
                Collaboration::PackageFile(Package), false, Error)))
        {
            AddError(Error);
            return false;
        }
        FText ReloadError;
        if (!TestTrue(TEXT("reload serialized packages"), UPackageTools::ReloadPackages({ Package, ActionPackage },
            ReloadError, EReloadPackagesInteractionMode::AssumePositive)))
        {
            AddError(ReloadError.ToString());
            return false;
        }
        auto* Reloaded = FindObject<UBlueprint>(nullptr, *BlueprintPath);
        if (!TestNotNull(TEXT("cold Blueprint exists"), Reloaded))
        {
            return false;
        }
        int32 Count = 0;
        for (UEdGraphNode* Node : Reloaded->UbergraphPages[0]->Nodes)
        {
            if (!IsEnhancedInputValueClass(Node->GetClass()))
            {
                continue;
            }
            ++Count;
            const auto Config = Transcode::NodeConfigJson(Reloaded, Node);
            TestEqual(TEXT("cold binding is exported"), Config->GetStringField(TEXT("input_action")), ActionPath);
            const auto* Property = CastFieldChecked<FObjectPropertyBase>(Node->GetClass()->FindPropertyByName(TEXT("InputAction")));
            auto* Bound = CastChecked<UInputAction>(Property->GetObjectPropertyValue_InContainer(Node));
            TestEqual(TEXT("cold value type is preserved"), static_cast<int32>(Bound->ValueType), Index);
            const auto* Output = Node->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
            if (!TestNotNull(TEXT("cold output pin exists"), Output))
            {
                return false;
            }
            if (Node->GetClass()->GetPathName().EndsWith(TEXT("K2Node_GetInputActionValue")))
            {
                const FName Category = Index == 0 ? UEdGraphSchema_K2::PC_Boolean
                    : Index == 1 ? UEdGraphSchema_K2::PC_Real : UEdGraphSchema_K2::PC_Struct;
                TestEqual(TEXT("cold typed output category"), Output->PinType.PinCategory, Category);
                if (Index == 1)
                {
                    TestEqual(TEXT("axis output retains double precision"), Output->PinType.PinSubCategory, UEdGraphSchema_K2::PC_Double);
                }
                if (Index >= 2)
                {
                    const auto* Struct = Index == 2 ? TBaseStructure<FVector2D>::Get() : TBaseStructure<FVector>::Get();
                    TestTrue(TEXT("cold vector output structure"), Output->PinType.PinSubCategoryObject == Struct);
                }
            }
            else
            {
                if (!TestNotNull(TEXT("accessor native value structure exists"), Output->PinType.PinSubCategoryObject.Get()))
                {
                    return false;
                }
                TestEqual(TEXT("accessor output retains its native value structure"),
                    Output->PinType.PinSubCategoryObject->GetName(), FString(TEXT("InputActionValue")));
            }
            if (auto* Pin = Node->FindPin(TEXT("Action")))
            {
                TestTrue(TEXT("accessor Action pin survives compilation and reload"), Pin->DefaultObject == Bound);
            }
        }
        TestEqual(TEXT("both current-value hosts survive"), Count, 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInputValueRuntime,
    "Nexus.Issues7.Input.CurrentValueRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInputValueRuntime::RunTest(const FString& Parameters)
{
    const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        World->RemoveFromRoot();
    };
    auto* Controller = World->SpawnActor<APlayerController>();
    auto* Input = NewObject<UEnhancedPlayerInput>(Controller);
    Controller->PlayerInput = Input;
    auto* Actor = World->SpawnActor<AActor>();
    auto* Component = NewObject<UEnhancedInputComponent>(Actor);
    Actor->InputComponent = Component;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        auto* Action = NewObject<UInputAction>();
        Action->ValueType = static_cast<EInputActionValueType>(Index);
        Component->BindActionValue(Action);
        const FInputActionValue Expected(Action->ValueType, FVector(0.75, 0.5, 0.25));
        Input->InjectInputForAction(Action, Expected);
        Input->ProcessInputStack({ Component }, 1.0f / 60.0f, false);
        const auto Actual = UEnhancedInputLibrary::GetBoundActionValue(Actor, Action);
        TestEqual(TEXT("bound action keeps its value type"), Actual.GetValueType(), Expected.GetValueType());
        TestEqual(TEXT("native input processing reaches current-value accessor"), Actual.Get<FVector>(), Expected.Get<FVector>());
    }
    return true;
}
#endif
