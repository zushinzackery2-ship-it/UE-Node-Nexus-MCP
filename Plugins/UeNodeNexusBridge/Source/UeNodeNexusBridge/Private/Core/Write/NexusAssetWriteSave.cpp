#include "UeNodeNexusBridgeSaveResult.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
FAssetWriteSave SaveAssetWrite(UObject* Asset, bool bRequested, bool bDryRun, bool bChanged)
{
    FAssetWriteSave Save;
    if (bDryRun || !bRequested)
    {
        return Save;
    }
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    Save.bMemoryChanged = bChanged || (Package && Package->IsDirty());
    Save.bAttempted = Save.bMemoryChanged;
    if (Save.bAttempted)
    {
        Save.bSaved = Transcode::SavePackageDirect(Package, Asset, Save.Error, &Save.Code);
    }
    return Save;
}

void ApplyAssetWriteSave(const TSharedPtr<FJsonObject>& Response, const TSharedPtr<FJsonObject>& Data, const FAssetWriteSave& Save)
{
    Data->SetBoolField(TEXT("saved"), Save.bSaved);
    Data->SetBoolField(TEXT("save_attempted"), Save.bAttempted);
    Data->SetBoolField(TEXT("save_failed"), Save.Failed());
    Data->SetBoolField(TEXT("partial"), Save.Failed() && Save.bMemoryChanged);
    if (Save.Failed())
    {
        Data->SetStringField(TEXT("save_error"), Save.Error);
        Data->SetStringField(TEXT("save_code"), Save.Code);
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_failed"), Save.Error));
    }
}
}
