#include "NexusRuntimeDiagnostics.h"

#include "Editor.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
namespace
{
FDelegateHandle BeginHandle;
FDelegateHandle EndHandle;
class FRuntimeOutput final : public FOutputDevice
{
public:
    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }
    virtual bool CanBeUsedOnMultipleThreads() const override
    {
        return true;
    }
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Level, const FName& Category) override
    {
        if (Level > ELogVerbosity::Warning || (Category != TEXT("LogScript") && Category != TEXT("LogRHI")
            && Category != TEXT("LogD3D12RHI") && Category != TEXT("LogShaderCompilers")))
        {
            return;
        }
        auto Event = MakeShared<FJsonObject>();
        Event->SetStringField(TEXT("source"), Category.ToString());
        Event->SetStringField(TEXT("message"), FString(Text).Left(4096));
        Event->SetStringField(TEXT("severity"), Level == ELogVerbosity::Fatal ? TEXT("fatal")
            : Level == ELogVerbosity::Error ? TEXT("error") : TEXT("warning"));
        Event->SetStringField(TEXT("code"), TEXT("runtime_log"));
        Record(Event);
    }
};
FRuntimeOutput Output;
}

void Start()
{
    BeginSession(false);
    StartMessages();
    GLog->AddOutputDevice(&Output);
    SetCaptureReady(true);
    BeginHandle = FEditorDelegates::BeginPIE.AddLambda([](bool)
    {
        BeginSession(true);
    });
    EndHandle = FEditorDelegates::EndPIE.AddLambda([](bool)
    {
        EndSession();
        BeginSession(false);
    });
}

void Stop()
{
    SetCaptureReady(false);
    FEditorDelegates::BeginPIE.Remove(BeginHandle);
    FEditorDelegates::EndPIE.Remove(EndHandle);
    GLog->RemoveOutputDevice(&Output);
    StopMessages();
    EndSession();
}
}
