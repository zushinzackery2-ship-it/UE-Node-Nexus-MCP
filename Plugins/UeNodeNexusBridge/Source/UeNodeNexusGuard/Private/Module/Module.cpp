#include "Modules/ModuleManager.h"
#include "../Identity/Identity.h"
#include "../Control/Pipe.h"

class FUeNodeNexusGuardModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (NexusLifecycle::InitializeIdentity())
        {
            Server = MakeUnique<NexusLifecycle::FPipe>();
        }
    }

    virtual void ShutdownModule() override
    {
        Server.Reset();
        NexusLifecycle::ReleaseIdentity();
    }
private:
    TUniquePtr<NexusLifecycle::FPipe> Server;
};

IMPLEMENT_MODULE(FUeNodeNexusGuardModule, UeNodeNexusGuard)
