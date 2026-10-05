#include "UeNodeNexusBridgeModule.h"

#include "UeNodeNexusBridgeAutoIndex.h"
#include "UeNodeNexusBridgeCoreOperationsRegistry.h"
#include "UeNodeNexusBridgeNamedPipeServer.h"
#include "UeNodeNexusBridgeTranscodeWatch.h"
#include "Level/Instances/NexusInstanceIdentity.h"
#include "Core/BuildInfo/NexusCoreBuildInfo.h"
#include "UeNodeNexusBridgeBuildInfo.h"
#include "Lifecycle/EditorLifecycle.h"
#include "Viewport/Capture/NexusCapture.h"
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "Diagnostics/Runtime/Validation/NexusRuntimeSmoke.h"
#include "Transport/Dispatch/NexusFrameQueue.h"

IMPLEMENT_MODULE(FUeNodeNexusBridgeModule, UeNodeNexusBridge)

void FUeNodeNexusBridgeModule::StartupModule()
{
    UeNodeNexusBridge::RegisterCoreBuildIdentity();
    UeNodeNexusBridge::Instances::StartupIdentity();
    UeNodeNexusBridge::RuntimeDiagnostics::Start();
    UeNodeNexusBridge::StartupAutoIndex();
    UeNodeNexusBridge::RegisterCoreOperations();
    UeNodeNexusBridge::RegisterAutoIndexOperations();
    UeNodeNexusBridge::StartRequestQueue();
    BridgeServer = MakeUnique<FUeNodeNexusBridgeNamedPipeServer>();
    BridgeServer->Start();
    UeNodeNexusBridge::Lifecycle::Start();
}

void FUeNodeNexusBridgeModule::ShutdownModule()
{
    UeNodeNexusBridge::StopRequestQueue();
    UeNodeNexusBridge::Capture::Stop();
    UeNodeNexusBridge::RuntimeSmoke::Stop();
    UeNodeNexusBridge::Lifecycle::Stop();
    UeNodeNexusBridge::RuntimeDiagnostics::Stop();
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
