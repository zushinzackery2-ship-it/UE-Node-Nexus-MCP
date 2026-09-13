#pragma once

#include "UeNodeNexusCollaboration.h"

namespace UeNodeNexusBridge::Collaboration
{
FString Text(const FJson& Json, const TCHAR* Key);
bool Flag(const FJson& Json, const TCHAR* Key, bool Default = false);
FJson Object(const FJson& Json, const TCHAR* Key);
TArray<TSharedPtr<FJsonValue>> Rows(const FJson& Json, const TCHAR* Key);
FString TransactionDirectory(const FString& ApplyId);
bool ReadJournal(const FString& File, FJson& Json);
bool WriteJournal(const FString& File, const FJson& Json, FString& Error);
bool SaveReceipt(const FJson& Receipt, const FString& Phase, FString& Error);
bool HasPending(const FJson& Request, const FString& ApplyId, FString& Error);
bool CheckRevisions(const FJson& Request, FJson& Current, FString& Error);
bool Checkpoint(const FJson& Request, const FJson& Before, const FJson& Receipt, FString& Error);
bool SavePackages(const FJson& Request, const FJson& Receipt, FString& Error);
bool RestoreCheckpoint(const FJson& Receipt, FString& Error);
FJson ResultSnapshot(const FJson& Request, const FJson& Response);
bool ExportResult(const FJson& Request, const FJson& Snapshot, const FJson& Data, FString& Error);
FJson CommitError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Error, const FJson& Receipt = nullptr);
}
