#include "UeNodeNexusBridgeTranscodeApi.h"

#include "AssetRegistry/AssetData.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge::Transcode
{
bool IsAssetDataCurrent(const FAssetData& AssetData)
{
    if (!AssetData.IsValid())
    {
        return false;
    }
    UObject* Loaded = AssetData.GetSoftObjectPath().ResolveObject();
    return !Loaded || (IsValid(Loaded) && Loaded->IsAsset());
}
}
