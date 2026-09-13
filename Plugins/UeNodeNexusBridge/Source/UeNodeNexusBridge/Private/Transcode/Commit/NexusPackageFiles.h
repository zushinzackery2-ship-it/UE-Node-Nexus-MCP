#pragma once

#include "NexusCommitInternal.h"

class UPackage;

namespace UeNodeNexusBridge::Collaboration
{
FString PackageFile(UPackage* Package);
FString FileHash(const FString& File);
bool CopyFile(const FString& Source, const FString& Target, FString& Error);
bool CapturePackage(UPackage* Package, const FJson& Receipt, FString& Error);
bool CheckRecoveryFiles(const FJson& Receipt, FString& Error);
bool RestorePackageFiles(const FJson& Package, bool bMemory, FString& Error);
bool SaveStagedPackage(UPackage* Package, const FJson& Receipt, const FJson& Row, FString& Error);
}
