#include "NexusFrameQueue.h"

#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "NexusLifecycle.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FPending
{
    TSharedPtr<FJsonObject> Request;
    TSharedPtr<TPromise<FString>> Promise;
};
FCriticalSection Mutex;
TArray<FPending> Pending;
FDelegateHandle FrameHandle;
bool bAccepting = false;
bool bPumping = false;

FString Reject(const TSharedPtr<FJsonObject>& Request, const FString& Reason)
{
    const FString Id = Request->GetStringField(TEXT("request_id"));
    auto Response = MakeEnvelope(Request->GetStringField(TEXT("operation")), Id, false);
    auto Error = UeNodeNexusBridge::MakeError(FString(TEXT("bridge_busy")), FString(TEXT("editor frame queue cannot admit the request")));
    Error->SetStringField(TEXT("busy_reason"), Reason);
    Response->SetObjectField(TEXT("error"), Error);
    NexusLifecycle::Complete(Id, false, TEXT("bridge_busy"), false);
    return SerializeJsonObjectToString(Response);
}
}

TFuture<FString> EnqueueBridgeRequest(const TSharedPtr<FJsonObject>& Request)
{
    auto Promise = MakeShared<TPromise<FString>>();
    auto Future = Promise->GetFuture();
    FString Reason;
    {
        FScopeLock Lock(&Mutex);
        if (bAccepting && Pending.Num() < 4)
        {
            Pending.Add(
                { Request, Promise });
            return Future;
        }
        Reason = bAccepting ? TEXT("frame_queue_full") : TEXT("frame_queue_stopped");
    }
    Promise->SetValue(Reject(Request, Reason));
    return Future;
}

void PumpRequestQueue()
{
    check(IsInGameThread());
    if (bPumping || IsBridgeDispatchActive())
    {
        return;
    }
    FPending Item;
    {
        FScopeLock Lock(&Mutex);
        if (!bAccepting || Pending.IsEmpty())
        {
            return;
        }
        Item = MoveTemp(Pending[0]);
        Pending.RemoveAt(0);
    }
    // OnBeginFrame runs before world ticking. TaskGraph pumping during a world
    // tick, GC, shader wait or modal dialog cannot execute a queued mutation.
    TGuardValue<bool> Guard(bPumping, true);
    Item.Promise->SetValue(DispatchParsedRequest(Item.Request));
}

void StartRequestQueue()
{
    check(IsInGameThread());
    FScopeLock Lock(&Mutex);
    check(!bAccepting);
    bAccepting = true;
    FrameHandle = FCoreDelegates::OnBeginFrame.AddStatic(&PumpRequestQueue);
}

void StopRequestQueue()
{
    check(IsInGameThread());
    FCoreDelegates::OnBeginFrame.Remove(FrameHandle);
    TArray<FPending> Cancelled;
    {
        FScopeLock Lock(&Mutex);
        bAccepting = false;
        Cancelled = MoveTemp(Pending);
    }
    for (const auto& Item : Cancelled)
    {
        Item.Promise->SetValue(Reject(Item.Request, TEXT("frame_queue_stopped")));
    }
}
}
