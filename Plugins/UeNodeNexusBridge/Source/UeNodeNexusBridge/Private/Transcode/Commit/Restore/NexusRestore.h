#pragma once

#include "Transcode/Commit/NexusPackageFiles.h"

namespace UeNodeNexusBridge::Collaboration
{
bool ValidateCheckpointFiles(const FJson& Receipt, FString& Error);
bool PrepareCheckpointPackages(const FJson& Receipt, FString& Error);
bool RestoreCheckpointMemory(const FJson& Receipt, FString& Error);
bool RestoreCheckpointDisk(const FJson& Receipt, FString& Error);
}
