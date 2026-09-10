#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeAutoIndex.h"
#include "UeNodeNexusBridgeCoreOperationsRegistry.h"
#include "UeNodeNexusBridgeNamedPipeServer.h"
#include "UeNodeNexusBridgeTranscodeWatch.h"
#include "Level/Instances/NexusInstanceIdentity.h"
#include "Core/BuildInfo/NexusCoreBuildInfo.h"
#include "UeNodeNexusBridgeBuildInfo.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    UeNodeNexusBridge::RegisterCoreBuildIdentity();
    UeNodeNexusBridge::Instances::StartupIdentity();
    UeNodeNexusBridge::StartupAutoIndex();
    UeNodeNexusBridge::RegisterCoreOperations();
    UeNodeNexusBridge::RegisterAutoIndexOperations();
    BridgeServer = MakeUnique<FUeNodeNexusBridgeNamedPipeServer>();
    BridgeServer->Start();
}

void FUeNodeNexusBridgeModule::ShutdownModule()
{
    UeNodeNexusBridge::ShutdownTranscodeWatch();
    UeNodeNexusBridge::Instances::ShutdownIdentity();
    if (BridgeServer.IsValid())
    {
        BridgeServer->Stop();
        BridgeServer.Reset();
    }
    UeNodeNexusBridge::UnregisterAutoIndexOperations();
    UeNodeNexusBridge::UnregisterCoreOperations();
    UeNodeNexusBridge::ShutdownAutoIndex();
    UeNodeNexusBridge::UnregisterBuildIdentity(TEXT("UeNodeNexusBridge"));
}
