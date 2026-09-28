#include "Modules/ModuleManager.h"
#include "../Identity/Identity.h"
#include "../Control/Pipe.h"
#include "../Control/State.h"
#include "../Startup/Monitor.h"

class FUeNodeNexusGuardModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (NexusLifecycle::InitializeIdentity())
        {
            NexusLifecycle::Startup::Install(NexusLifecycle::State().bManaged);
            Server = MakeUnique<NexusLifecycle::FPipe>();
        }
    }

    virtual void ShutdownModule() override
    {
        if (Server.IsValid())
        {
            NexusLifecycle::Startup::Uninstall();
        }
        Server.Reset();
        NexusLifecycle::ReleaseIdentity();
    }
private:
    TUniquePtr<NexusLifecycle::FPipe> Server;
};

IMPLEMENT_MODULE(FUeNodeNexusGuardModule, UeNodeNexusGuard)
