#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

namespace NexusLifecycle
{
class FPipe : public FRunnable
{
public:
    FPipe();
    virtual ~FPipe() override;
    virtual uint32 Run() override;
    virtual void Stop() override;
private:
    void* StopEvent = nullptr;
    void* Security = nullptr;
    TArray<FRunnableThread*> Threads;
    TUniquePtr<FRunnable> WatchdogTask;
};
}
