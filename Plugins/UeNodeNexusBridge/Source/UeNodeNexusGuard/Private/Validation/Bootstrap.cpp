#include "Environment.h"

#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace NexusLifecycle::Validation
{
namespace
{
TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
FDelegateHandle Completion;
}

TSharedPtr<FJsonObject> BootstrapStatus()
{
    return Status;
}

TSharedPtr<FJsonObject> Environment()
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("effective_environment_contract"), 1);
    Result->SetObjectField(TEXT("effective_configuration"), Configuration());
    Result->SetObjectField(TEXT("render_cvars"), ConsoleVariables());
    Result->SetObjectField(TEXT("render_launch"), RenderingLaunch());
    return Result;
}

void Install()
{
    Status = MakeShared<FJsonObject>();
    Status->SetBoolField(TEXT("requested"), false);
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Nexus/validation-worker.json");
    if (!FPaths::FileExists(Path))
    {
        return;
    }
    Status->SetBoolField(TEXT("requested"), true);
    FString Text, Project, Error;
    TSharedPtr<FJsonObject> Marker;
    const TSharedPtr<FJsonObject>* Expected = nullptr;
    const TSharedPtr<FJsonObject>* ConfigurationInput = nullptr;
    const TSharedPtr<FJsonObject>* Variables = nullptr;
    if (!FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Marker)
        || !Marker->TryGetStringField(TEXT("project_path"), Project)
        || !FPaths::IsSamePath(Project, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())))
    {
        Error = TEXT("validation marker does not identify the current project");
    }
    else if (!Marker->TryGetObjectField(TEXT("environment"), Expected))
    {
        // Disk-baseline safety_validate has no publishing process to reproduce.
        Status->SetBoolField(TEXT("requested"), false);
        return;
    }
    else if (!(*Expected)->TryGetObjectField(TEXT("effective_configuration"), ConfigurationInput)
        || !(*Expected)->TryGetObjectField(TEXT("render_cvars"), Variables)
        || !ValidateConfiguration(*ConfigurationInput, Error) || !ValidateConsoleVariables(*Variables, Error))
    {
        if (Error.IsEmpty())
        {
            Error = TEXT("validation environment contract is incomplete");
        }
    }
    if (!Error.IsEmpty())
    {
        Status->SetBoolField(TEXT("applied"), false);
        Status->SetStringField(TEXT("error"), Error);
        UE_LOG(LogTemp, Error, TEXT("Nexus validation bootstrap rejected: %s"), *Error);
        return;
    }
    ApplyConfiguration(*ConfigurationInput);
    const auto Captured = *Variables;
    const auto Pending = ApplyConsoleVariables(Captured);
    Status->SetBoolField(TEXT("applied"), false);
    Status->SetBoolField(TEXT("values_applied"), true);
    Status->SetBoolField(TEXT("callbacks_applied"), false);
    Status->SetStringField(TEXT("phase"), TEXT("PostConfigInit"));
    Completion = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([Captured, Pending]()
    {
        FString Error;
        const bool bCompleted = CompleteConsoleVariables(Captured, Pending, Error);
        Status->SetBoolField(TEXT("applied"), bCompleted);
        Status->SetBoolField(TEXT("callbacks_applied"), bCompleted);
        Status->SetStringField(TEXT("callbacks_phase"), TEXT("EngineLoopInitComplete"));
        Status->SetNumberField(TEXT("callback_count"), Pending.Num());
        if (!bCompleted)
        {
            Status->SetStringField(TEXT("error"), Error);
            UE_LOG(LogTemp, Error, TEXT("Nexus validation callback completion rejected: %s"), *Error);
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("Nexus validation callbacks completed after engine initialization count=%d"), Pending.Num());
        }
    });
    UE_LOG(LogTemp, Display, TEXT("Nexus validation bootstrap values applied before RHIInit configuration=%d cvars=%d"),
        (*ConfigurationInput)->Values.Num(), (*Variables)->Values.Num());
}

void Uninstall()
{
    FCoreDelegates::OnFEngineLoopInitComplete.Remove(Completion);
    Completion.Reset();
}
}
