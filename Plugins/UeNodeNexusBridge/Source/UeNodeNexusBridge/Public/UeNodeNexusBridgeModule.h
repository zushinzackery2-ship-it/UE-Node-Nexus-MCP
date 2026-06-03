#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

class FUeNodeNexusBridgeNamedPipeServer;

class FUeNodeNexusBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<FUeNodeNexusBridgeNamedPipeServer> BridgeServer;
};
