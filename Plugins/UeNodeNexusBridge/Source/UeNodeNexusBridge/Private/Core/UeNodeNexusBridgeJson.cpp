#include "UeNodeNexusBridgeJson.h"

#include "Containers/StringConv.h"
#include "HttpServerResponse.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    Error->SetObjectField(TEXT("details"), MakeShared<FJsonObject>());
    return Error;
}

TSharedPtr<FJsonObject> MakeEnvelope(const FString& Operation, const FString& RequestId, bool bOk)
{
    TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
    Envelope->SetBoolField(TEXT("ok"), bOk);
    Envelope->SetStringField(TEXT("operation"), Operation);
    Envelope->SetStringField(TEXT("request_id"), RequestId);
    Envelope->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
    Envelope->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
    return Envelope;
}

TUniquePtr<FHttpServerResponse> JsonResponse(const TSharedPtr<FJsonObject>& JsonObject)
{
    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return FHttpServerResponse::Create(Output, TEXT("application/json"));
}

FString BodyToString(const TArray<uint8>& Body)
{
    if (Body.Num() == 0)
    {
        return FString();
    }

    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
    return FString(Converted.Length(), Converted.Get());
}

bool TryGetPayload(const TSharedPtr<FJsonObject>& Envelope, TSharedPtr<FJsonObject>& OutPayload)
{
    const TSharedPtr<FJsonObject>* PayloadObject = nullptr;
    if (!Envelope->TryGetObjectField(TEXT("payload"), PayloadObject) || PayloadObject == nullptr)
    {
        return false;
    }

    OutPayload = *PayloadObject;
    return OutPayload.IsValid();
}

int32 ReadCursor(const TSharedPtr<FJsonObject>& Payload)
{
    FString Cursor;
    if (!Payload->TryGetStringField(TEXT("cursor"), Cursor) || Cursor.IsEmpty())
    {
        return 0;
    }

    return FMath::Max(0, FCString::Atoi(*Cursor));
}

int32 ReadLimit(const TSharedPtr<FJsonObject>& Payload, int32 DefaultLimit, int32 MaxLimit)
{
    double NumericLimit = static_cast<double>(DefaultLimit);
    if (!Payload->TryGetNumberField(TEXT("limit"), NumericLimit))
    {
        return DefaultLimit;
    }

    return FMath::Clamp(static_cast<int32>(NumericLimit), 1, MaxLimit);
}

TSharedPtr<FJsonObject> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message, const FString& AssetPath, const FString& Source)
{
    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
    Diagnostic->SetStringField(TEXT("source"), Source);
    Diagnostic->SetStringField(TEXT("raw"), Message);
    return Diagnostic;
}

TSharedPtr<FJsonObject> MakeEmptyDiff()
{
    TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
    Diff->SetArrayField(TEXT("nodes_created"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("nodes_deleted"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("links_added"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("links_removed"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("params_changed"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("components_added"), TArray<TSharedPtr<FJsonValue>>());
    Diff->SetArrayField(TEXT("components_removed"), TArray<TSharedPtr<FJsonValue>>());
    return Diff;
}

TSharedPtr<FJsonObject> MakeDirtyState(UObject* Asset)
{
    UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
    TSharedPtr<FJsonObject> DirtyState = MakeShared<FJsonObject>();
    DirtyState->SetBoolField(TEXT("package_dirty"), Package ? Package->IsDirty() : false);
    DirtyState->SetBoolField(TEXT("saved"), false);
    DirtyState->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    return DirtyState;
}

TSharedPtr<FJsonObject> MakePinIntegrity(bool bOk, const TArray<TSharedPtr<FJsonValue>>& BrokenLinks, const TArray<TSharedPtr<FJsonValue>>& MissingPins)
{
    TSharedPtr<FJsonObject> PinIntegrity = MakeShared<FJsonObject>();
    PinIntegrity->SetBoolField(TEXT("ok"), bOk);
    PinIntegrity->SetArrayField(TEXT("broken_links"), BrokenLinks);
    PinIntegrity->SetArrayField(TEXT("missing_pins"), MissingPins);
    return PinIntegrity;
}

TSharedPtr<FJsonObject> MakeCompilePostCheck(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount)
{
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetBoolField(TEXT("requested"), bRequested);
    Compile->SetBoolField(TEXT("ran"), bRan);
    Compile->SetBoolField(TEXT("ok"), bOk);
    Compile->SetNumberField(TEXT("error_count"), ErrorCount);
    Compile->SetNumberField(TEXT("warning_count"), WarningCount);
    return Compile;
}

static int32 CountTextLines(const FString& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }

    int32 Lines = 1;
    for (int32 Index = 0; Index < Text.Len(); ++Index)
    {
        if (Text[Index] == TEXT('\n'))
        {
            ++Lines;
        }
    }
    if (Text.EndsWith(TEXT("\n")))
    {
        --Lines;
    }
    return Lines;
}

static int32 CountDiffArrayField(const TSharedPtr<FJsonObject>& Diff, const FString& FieldName)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (Diff.IsValid() && Diff->TryGetArrayField(FieldName, Items) && Items != nullptr)
    {
        return Items->Num();
    }
    return 0;
}

static TSharedPtr<FJsonObject> MakeCompactDiff(const TSharedPtr<FJsonObject>& Diff)
{
    static const FString FieldNames[] = {
        TEXT("nodes_created"),
        TEXT("nodes_deleted"),
        TEXT("links_added"),
        TEXT("links_removed"),
        TEXT("params_changed"),
        TEXT("components_added"),
        TEXT("components_removed"),
        TEXT("assets_created"),
        TEXT("assets_deleted"),
        TEXT("assets_moved"),
        TEXT("assets_renamed"),
        TEXT("folders_created"),
        TEXT("folders_deleted"),
        TEXT("redirectors_fixed"),
    };

    TSharedPtr<FJsonObject> Compact = MakeShared<FJsonObject>();
    Compact->SetStringField(TEXT("format"), TEXT("compact"));

    int32 TotalChanges = 0;
    FString Text(TEXT("D:"));
    for (const FString& FieldName : FieldNames)
    {
        const int32 Count = CountDiffArrayField(Diff, FieldName);
        TotalChanges += Count;
        Compact->SetNumberField(FieldName + TEXT("_count"), Count);
        Text += FString::Printf(TEXT("%s=%d|"), *FieldName, Count);
    }
    Compact->SetNumberField(TEXT("total_changes"), TotalChanges);
    Text += FString::Printf(TEXT("total=%d"), TotalChanges);
    SetTextPayload(Compact, Text);
    return Compact;
}

TSharedPtr<FJsonObject> MakeWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState)
{
    return MakeWriteDataWithDiffFormat(bDryRun, bApplied, bChanged, Diff, PinIntegrity, Compile, DirtyState, TEXT("full"));
}

TSharedPtr<FJsonObject> MakeWriteDataWithDiffFormat(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState, const FString& DiffFormat)
{
    TSharedPtr<FJsonObject> PostChecks = MakeShared<FJsonObject>();
    PostChecks->SetObjectField(TEXT("pin_integrity"), PinIntegrity);
    PostChecks->SetObjectField(TEXT("compile"), Compile);
    PostChecks->SetObjectField(TEXT("dirty_state"), DirtyState);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApplied);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetObjectField(TEXT("diff"), DiffFormat.Equals(TEXT("full"), ESearchCase::IgnoreCase) ? Diff : MakeCompactDiff(Diff));
    Data->SetObjectField(TEXT("post_checks"), PostChecks);
    return Data;
}

void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text)
{
    const FTCHARToUTF8 Utf8Text(*Text);
    const int32 TextBytes = Utf8Text.Length();
    Data->SetStringField(TEXT("text"), Text);
    Data->SetNumberField(TEXT("text_bytes"), TextBytes);
    Data->SetNumberField(TEXT("text_kib"), static_cast<double>(TextBytes) / 1024.0);
    Data->SetNumberField(TEXT("text_lines"), CountTextLines(Text));
}
}
