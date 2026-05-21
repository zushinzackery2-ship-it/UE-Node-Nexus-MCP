#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeAutoIndex.h"
#include "UeNodeNexusBridgeHttpServer.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    UeNodeNexusBridge::StartupAutoIndex();
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
    UeNodeNexusBridge::ShutdownAutoIndex();
}
