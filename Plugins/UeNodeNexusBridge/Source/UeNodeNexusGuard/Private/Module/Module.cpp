#include "Modules/ModuleManager.h"
#include "../Identity/Identity.h"
#include "../Control/Pipe.h"
#include "../Control/State.h"
#include "../Startup/Monitor.h"
#include "NexusValidation.h"

class FUeNodeNexusGuardModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (NexusLifecycle::InitializeIdentity())
        {
            NexusLifecycle::Validation::Install();
            NexusLifecycle::Startup::Install(NexusLifecycle::State().bManaged);
            Server = MakeUnique<NexusLifecycle::FPipe>();
        }
    }

    virtual void ShutdownModule() override
    {
        if (Server.IsValid())
        {
            // Status readers retain the startup observer until every pipe and
            // watchdog worker has joined. Only then may its log sink be freed.
            Server.Reset();
            NexusLifecycle::Startup::Uninstall();
        }
        NexusLifecycle::Validation::Uninstall();
        NexusLifecycle::ReleaseIdentity();
    }
private:
    TUniquePtr<NexusLifecycle::FPipe> Server;
};

IMPLEMENT_MODULE(FUeNodeNexusGuardModule, UeNodeNexusGuard)
