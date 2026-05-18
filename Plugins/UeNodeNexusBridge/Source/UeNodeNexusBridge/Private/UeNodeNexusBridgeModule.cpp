#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeHttpServer.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    BridgeServer = MakeUnique<FUeNodeNexusBridgeHttpServer>();
    BridgeServer->Start();
}

void FUeNodeNexusBridgeModule::ShutdownModule()
{
    if (BridgeServer.IsValid())
    {
        BridgeServer->Stop();
        BridgeServer.Reset();
    }
}

