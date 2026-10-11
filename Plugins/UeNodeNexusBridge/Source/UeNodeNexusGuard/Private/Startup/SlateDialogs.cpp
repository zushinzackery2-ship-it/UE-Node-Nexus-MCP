#include "SlateDialogs.h"
#include "../Control/State.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/ScopeLock.h"
#include "Widgets/SWindow.h"

namespace NexusLifecycle::SlateDialogs
{
namespace
{
FDelegateHandle PreTick;
FDelegateHandle ModalTick;
TWeakPtr<SWindow> CurrentWindow;
TSharedPtr<FJsonObject> Current;
TArray<TWeakPtr<SWindow>> NonBlockingWindows;

void Observe(bool bModalLoop)
{
    check(IsInGameThread());
    auto Window = FSlateApplication::Get().GetActiveModalWindow();
    // Slow tasks share Slate's modal stack but keep the game thread running.
    // Only the nested modal loop identifies a window awaiting a user decision.
    if (!bModalLoop && CurrentWindow.Pin() != Window)
    {
        Window.Reset();
    }
    if ((Window && CurrentWindow.Pin() == Window) || (!Window && !Current))
    {
        return;
    }
    TSharedPtr<FJsonObject> Dialog;
    if (Window)
    {
        Dialog = MakeShared<FJsonObject>();
        Dialog->SetStringField(TEXT("source"), TEXT("slate_modal"));
        Dialog->SetStringField(TEXT("title"), Window->GetTitle().ToString());
        Dialog->SetStringField(TEXT("buttons"), TEXT("editor_ui"));
        Dialog->SetStringField(TEXT("opened_at"), FDateTime::UtcNow().ToIso8601());
        const auto Native = Window->GetNativeWindow();
        Dialog->SetStringField(TEXT("native_handle"), FString::Printf(TEXT("%llu"),
            static_cast<uint64>(reinterpret_cast<UPTRINT>(Native ? Native->GetOSWindowHandle() : nullptr))));
    }
    FScopeLock Lock(&State().Mutex);
    CurrentWindow = Window;
    Current = MoveTemp(Dialog);
}

void ObserveNormalTick(float)
{
    NonBlockingWindows.RemoveAll([](const TWeakPtr<SWindow>& Window)
    {
        return !Window.IsValid();
    });
    const auto Window = FSlateApplication::Get().GetActiveModalWindow();
    if (Window && CurrentWindow.Pin() != Window)
    {
        NonBlockingWindows.AddUnique(TWeakPtr<SWindow>(Window));
    }
    Observe(false);
}

void ObserveModalTick(float)
{
    // Another modal listener can close a dialog before our listener runs.
    // Its restored progress parent was already observed in a normal tick.
    const auto Window = FSlateApplication::Get().GetActiveModalWindow();
    Observe(!NonBlockingWindows.Contains(TWeakPtr<SWindow>(Window)));
}
}

void Install()
{
    if (!IsInGameThread() || PreTick.IsValid() || !FSlateApplication::IsInitialized())
    {
        return;
    }
    auto& Slate = FSlateApplication::Get();
    PreTick = Slate.OnPreTick().AddStatic(&ObserveNormalTick);
    ModalTick = Slate.GetOnModalLoopTickEvent().AddStatic(&ObserveModalTick);
    ObserveNormalTick(0.0f);
}

void Uninstall()
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnPreTick().Remove(PreTick);
        FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(ModalTick);
    }
    PreTick.Reset();
    ModalTick.Reset();
    FScopeLock Lock(&State().Mutex);
    Current.Reset();
    CurrentWindow.Reset();
    NonBlockingWindows.Reset();
}

TSharedPtr<FJsonObject> OpenLocked()
{
    if (Current)
    {
        return MakeShared<FJsonObject>(*Current);
    }
    return nullptr;
}
}
