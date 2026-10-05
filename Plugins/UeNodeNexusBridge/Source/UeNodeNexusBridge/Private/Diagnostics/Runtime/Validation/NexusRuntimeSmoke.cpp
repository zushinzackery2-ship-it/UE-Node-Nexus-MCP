#include "NexusRuntimeSmoke.h"
#include "Verdict.h"

#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "Transport/Safety/NexusAsyncWork.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "Viewport/Capture/NexusCapture.h"
#include "UeNodeNexusBridgeJson.h"
#include "AssetCompilingManager.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlayInEditorDataTypes.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

namespace UeNodeNexusBridge::RuntimeSmoke
{
namespace
{
struct FJob
{
    TSharedPtr<FJsonObject> Receipt;
    TSharedPtr<FJsonObject> Capture;
    TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
    TWeakObjectPtr<UWorld> World;
    FString Request;
    FString Failure;
    double Deadline = 0;
    double Duration = 0;
    uint64 FirstFrame = 0;
    uint64 Frames = 0;
    int32 MinFrames = 0;
};
TUniquePtr<FJob> Active;
FTSTicker::FDelegateHandle Ticker;

TSharedPtr<FJsonObject> Load(const FString& Path)
{
    FString Text;
    TSharedPtr<FJsonObject> Result;
    if (!FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result))
    {
        return nullptr;
    }
    return Result;
}

bool Save()
{
    return FFileHelper::SaveStringToFile(SerializeJsonObjectToString(Active->Receipt),
        *Active->Receipt->GetStringField(TEXT("receipt_path")));
}

void Phase(const FString& State)
{
    Active->Receipt->SetStringField(TEXT("state"), State);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s runtime_smoke phase=%s"), *Active->Request, *State);
    if (!Save())
    {
        Active->Failure = TEXT("runtime_smoke_journal_failed");
    }
}

void Finish()
{
    auto Payload = MakeShared<FJsonObject>();
    FString Session;
    Active->Receipt->TryGetStringField(TEXT("session_id"), Session);
    Payload->SetStringField(TEXT("session_id"), Session.IsEmpty() ? TEXT("unobserved") : Session);
    Payload->SetNumberField(TEXT("limit"), 512);
    auto Runtime = RuntimeDiagnostics::Read(Payload);
    auto Verdict = Evaluate(Runtime, Session);
    Active->Receipt->SetObjectField(TEXT("runtime_verification"), Verdict);
    if (Active->Failure.IsEmpty() && !Verdict->GetBoolField(TEXT("passed")))
    {
        Active->Failure = Verdict->GetStringField(TEXT("code"));
    }
    Active->Receipt->SetObjectField(TEXT("runtime"), Runtime);
    Active->Receipt->SetNumberField(TEXT("frames"), Active->Frames);
    Active->Receipt->SetBoolField(TEXT("ok"), Active->Failure.IsEmpty());
    Active->Receipt->SetBoolField(TEXT("done"), true);
    Active->Receipt->SetStringField(TEXT("error_code"), Active->Failure);
    Active->Receipt->SetStringField(TEXT("finished_at"), FDateTime::UtcNow().ToIso8601());
    Phase(Active->Failure.IsEmpty() ? TEXT("completed") : TEXT("failed"));
    Safety::FinishAsync(Active->Request, Active->Failure.IsEmpty(), Active->Failure);
    Active.Reset();
}

bool Tick(float)
{
    if (!Active)
    {
        return false;
    }
    if (IsBridgeDispatchActive() || !Safety::EngineBusyReason().IsEmpty())
    {
        return true;
    }
    const FString State = Active->Receipt->GetStringField(TEXT("state"));
    if (State == TEXT("starting") && GEditor->PlayWorld)
    {
        Active->World = GEditor->PlayWorld;
    }
    if (FPlatformTime::Seconds() >= Active->Deadline && Active->Failure.IsEmpty())
    {
        Active->Failure = TEXT("runtime_smoke_timeout");
    }
    if (!Active->Failure.IsEmpty() && State != TEXT("stopping"))
    {
        Capture::Stop();
        GEditor->CancelRequestPlaySession();
        if (Active->World.IsValid() && GEditor->PlayWorld == Active->World.Get())
        {
            GEditor->RequestEndPlayMap();
        }
        Phase(TEXT("stopping"));
    }
    else if (State == TEXT("queued"))
    {
        if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0
            || (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()))
        {
            return true;
        }
        auto Viewport = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetFirstActiveViewport();
        if (!Viewport)
        {
            Active->Failure = TEXT("viewport_unavailable");
            return true;
        }
        Active->Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
        Active->Settings->SetPlayNetMode(PIE_Standalone);
        Active->Settings->SetPlayNumberOfClients(1);
        Active->Settings->SetRunUnderOneProcess(true);
        FRequestPlaySessionParams Params;
        Params.EditorPlaySettings = Active->Settings.Get();
        Params.bAllowOnlineSubsystem = false;
        Params.DestinationSlateViewport = TWeakPtr<IAssetViewport>(Viewport);
        GEditor->RequestPlaySession(Params);
        Phase(TEXT("starting"));
    }
    else if (State == TEXT("starting") && GEditor->PlayWorld)
    {
        Active->World = GEditor->PlayWorld;
        Active->FirstFrame = GFrameCounter;
        const auto Runtime = RuntimeDiagnostics::Snapshot();
        Active->Receipt->SetStringField(TEXT("session_id"), Runtime->GetStringField(TEXT("session_id")));
        Active->Receipt->SetStringField(TEXT("instance_id"), Runtime->GetStringField(TEXT("instance_id")));
        Phase(TEXT("running"));
    }
    else if (State == TEXT("running"))
    {
        if (!Active->World.IsValid() || GEditor->PlayWorld != Active->World.Get())
        {
            Active->Failure = TEXT("runtime_world_interrupted");
            return true;
        }
        Active->Frames = GFrameCounter - Active->FirstFrame;
        if (Active->World->GetTimeSeconds() >= Active->Duration && Active->Frames >= static_cast<uint64>(Active->MinFrames))
        {
            Active->Capture = Capture::Submit(Active->Request, TEXT("RuntimeSmoke"), TEXT("pie"), false, 30, false, Active->Failure, true);
            if (Active->Capture)
            {
                Active->Receipt->SetObjectField(TEXT("capture"), Active->Capture);
                Phase(TEXT("capturing"));
            }
        }
    }
    else if (State == TEXT("capturing"))
    {
        auto Capture = Load(Active->Capture->GetStringField(TEXT("receipt_path")));
        if (Capture && (Capture->GetStringField(TEXT("state")) == TEXT("completed")
            || Capture->GetStringField(TEXT("state")) == TEXT("failed")))
        {
            Active->Receipt->SetObjectField(TEXT("capture"), Capture);
            if (Capture->GetStringField(TEXT("state")) != TEXT("completed"))
            {
                Active->Failure = TEXT("runtime_render_failed");
            }
            GEditor->RequestEndPlayMap();
            Phase(TEXT("stopping"));
        }
    }
    else if (State == TEXT("stopping") && !GEditor->PlayWorld && !GEditor->GetPlaySessionRequest().IsSet())
    {
        Finish();
    }
    return Active.IsValid();
}
}

TSharedPtr<FJsonObject> Start(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    auto Worker = Load(FPaths::ProjectSavedDir() / TEXT("Nexus/validation-worker.json"));
    if (!Worker || !FPaths::IsSamePath(Worker->GetStringField(TEXT("project_path")), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())))
    {
        return MakeOperationError(Operation, RequestId, TEXT("runtime_worker_required"), TEXT("PIE smoke must execute inside safety_validate's copied project"));
    }
    double Duration = 2, Frames = 30, Timeout = 120;
    bool bDryRun = true;
    Payload->TryGetNumberField(TEXT("duration_seconds"), Duration);
    Payload->TryGetNumberField(TEXT("min_frames"), Frames);
    Payload->TryGetNumberField(TEXT("timeout_seconds"), Timeout);
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    if (!FMath::IsFinite(Duration) || !FMath::IsFinite(Frames) || !FMath::IsFinite(Timeout)
        || Duration < 1 || Duration > 30 || Frames < 1 || Frames > 600 || Frames != FMath::FloorToDouble(Frames)
        || Timeout < 30 || Timeout > 180)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("duration 1..30, frames 1..600, timeout 30..180 required"));
    }
    if (!GEditor || GEditor->PlayWorld || GEditor->GetPlaySessionRequest().IsSet() || Active
        || !GDynamicRHI || (FString(GDynamicRHI->GetName()) != TEXT("D3D12") && FString(GDynamicRHI->GetName()) != TEXT("D3D11")))
    {
        return MakeOperationError(Operation, RequestId, TEXT("runtime_smoke_not_ready"), TEXT("smoke requires an idle editor with D3D12 or D3D11"));
    }
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("smoke_id"), Id);
    Receipt->SetStringField(TEXT("rhi"), GDynamicRHI->GetName());
    Receipt->SetStringField(TEXT("validation_id"), Worker->GetStringField(TEXT("validation_id")));
    Receipt->SetStringField(TEXT("receipt_path"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Nexus/Validation") / (Id + TEXT(".json"))));
    Receipt->SetStringField(TEXT("state"), bDryRun ? TEXT("preview") : TEXT("queued"));
    Receipt->SetBoolField(TEXT("done"), false);
    Receipt->SetBoolField(TEXT("ok"), true);
    if (!bDryRun)
    {
        Active = MakeUnique<FJob>();
        Active->Receipt = Receipt;
        Active->Request = RequestId;
        Active->Duration = Duration;
        Active->MinFrames = static_cast<int32>(Frames);
        Active->Deadline = FPlatformTime::Seconds() + Timeout;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Receipt->GetStringField(TEXT("receipt_path"))), true);
        if (!Save() || !Safety::BeginAsync(RequestId, Operation))
        {
            Active.Reset();
            return MakeOperationError(Operation, RequestId, TEXT("runtime_smoke_not_admitted"), TEXT("could not persist or reserve smoke validation"));
        }
        Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
    }
    auto Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeShared<FJsonObject>(*Receipt));
    return Response;
}

TSharedPtr<FJsonObject> Status(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Id;
    Payload->TryGetStringField(TEXT("smoke_id"), Id);
    FGuid Guid;
    if (!FGuid::ParseExact(Id, EGuidFormats::Digits, Guid))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("smoke_id must be a GUID"));
    }
    auto Receipt = Load(FPaths::ProjectSavedDir() / TEXT("Nexus/Validation") / (Id + TEXT(".json")));
    if (!Receipt || Receipt->GetStringField(TEXT("smoke_id")) != Id)
    {
        return MakeOperationError(Operation, RequestId, TEXT("runtime_smoke_not_found"), TEXT("smoke receipt is unavailable"));
    }
    auto Response = MakeEnvelope(Operation, RequestId, !Receipt->GetBoolField(TEXT("done")) || Receipt->GetBoolField(TEXT("ok")));
    Response->SetObjectField(TEXT("data"), Receipt);
    if (!Response->GetBoolField(TEXT("ok")))
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Receipt->GetStringField(TEXT("error_code")), FString(TEXT("PIE/rendered-frame validation failed"))));
    }
    return Response;
}

void Stop()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (Active)
    {
        Active->Failure = TEXT("runtime_smoke_interrupted");
        Finish();
    }
}
}
