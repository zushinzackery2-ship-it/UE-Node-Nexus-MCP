#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeAutoIndex.h"
#include "UeNodeNexusBridgeCoreOperationsRegistry.h"
#include "UeNodeNexusBridgeHttpServer.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    UeNodeNexusBridge::StartupAutoIndex();
    UeNodeNexusBridge::RegisterCoreOperations();
    UeNodeNexusBridge::RegisterAutoIndexOperations();
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
    UeNodeNexusBridge::UnregisterAutoIndexOperations();
    UeNodeNexusBridge::UnregisterCoreOperations();
    UeNodeNexusBridge::ShutdownAutoIndex();
}
