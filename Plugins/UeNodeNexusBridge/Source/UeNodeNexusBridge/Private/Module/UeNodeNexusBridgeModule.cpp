#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeAutoIndex.h"
#include "UeNodeNexusBridgeCoreOperationsRegistry.h"
#include "UeNodeNexusBridgeNamedPipeServer.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    UeNodeNexusBridge::StartupAutoIndex();
    UeNodeNexusBridge::RegisterCoreOperations();
    UeNodeNexusBridge::RegisterAutoIndexOperations();
    BridgeServer = MakeUnique<FUeNodeNexusBridgeNamedPipeServer>();
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
