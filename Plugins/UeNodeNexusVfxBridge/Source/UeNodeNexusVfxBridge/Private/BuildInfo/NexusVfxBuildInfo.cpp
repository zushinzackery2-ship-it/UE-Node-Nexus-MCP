#include "NexusVfxBuildInfo.h"

#include "UeNodeNexusBridgeBuildInfo.h"

namespace UeNodeNexusBridge
{
void RegisterVfxBuildIdentity()
{
    FBridgeBuildIdentity Identity;
    Identity.Version = TEXT(NEXUS_BUILD_VERSION);
    Identity.SourceCommit = TEXT(NEXUS_SOURCE_COMMIT);
    Identity.SourceFingerprint = TEXT(NEXUS_SOURCE_FINGERPRINT);
    Identity.bSourceDirty = NEXUS_SOURCE_DIRTY != 0;
    Identity.bRecorded = NEXUS_IDENTITY_RECORDED != 0;
    Identity.ContractVersion = NEXUS_CONTRACT_VERSION;
    RegisterBuildIdentity(TEXT("UeNodeNexusVfxBridge"), Identity);
}
}
