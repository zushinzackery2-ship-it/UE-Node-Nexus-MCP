#include "State.h"

#include "Misc/ScopeLock.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
void SetCaptureReady(bool bReady)
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    Data.bCaptureReady = bReady;
    if (!Data.Sessions.IsEmpty() && Data.Sessions.Last().bActive)
    {
        auto& Current = Data.Sessions.Last();
        if (!Current.bPie || !bReady)
        {
            Current.bSourcesComplete = bReady;
        }
    }
}

void BeginSession(bool bPie)
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    if (!Data.Sessions.IsEmpty())
    {
        Data.Sessions.Last().bActive = false;
    }
    if (Data.Sessions.Num() == 16)
    {
        Data.Sessions.RemoveAt(0);
    }
    Store::FSession Session;
    Session.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    Session.bPie = bPie;
    Session.bSourcesComplete = Data.bCaptureReady;
    Data.Sessions.Add(MoveTemp(Session));
}

void EndSession()
{
    auto& Data = Store::State();
    FScopeLock Lock(&Data.Mutex);
    if (!Data.Sessions.IsEmpty())
    {
        Data.Sessions.Last().bActive = false;
    }
}
}
