#include "NexusCapture.h"

#include "Dom/JsonValue.h"

namespace UeNodeNexusBridge::Capture
{
namespace
{
const TCHAR* const ScreenshotNamePattern = TEXT("^[A-Za-z0-9_-]{1,100}$");
const TCHAR* const CaptureTargets[] =
{
    TEXT("level"), TEXT("active"), TEXT("pie")
};

bool IsScreenshotNameChar(TCHAR Char)
{
    return (Char >= TEXT('A') && Char <= TEXT('Z')) || (Char >= TEXT('a') && Char <= TEXT('z'))
        || (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('_') || Char == TEXT('-');
}

TSharedPtr<FJsonObject> RequestViolation(const FString& Field, const FString& Rule, FString& Message)
{
    Message = Rule;
    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("field"), Field);
    return Details;
}

TSharedPtr<FJsonObject> TypeViolation(const FString& Field, const FString& Type, FString& Message)
{
    TSharedPtr<FJsonObject> Details = RequestViolation(Field, FString::Printf(TEXT("%s must be a %s"), *Field, *Type), Message);
    Details->SetStringField(TEXT("expected_type"), Type);
    return Details;
}

const TCHAR* ScreenshotNameFault(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > MaxScreenshotNameLength)
    {
        return TEXT("length");
    }
    if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(":")))
    {
        return TEXT("directory");
    }
    return Name.Contains(TEXT(".")) ? TEXT("extension") : TEXT("character");
}

TSharedPtr<FJsonObject> NameViolation(const FString& Name, FString& Message)
{
    TSharedPtr<FJsonObject> Details = RequestViolation(TEXT("filename"),
        TEXT("filename must be 1..100 letters, digits, '_' or '-' without directory or extension"), Message);
    Details->SetStringField(TEXT("value"), Name.Left(MaxScreenshotNameLength + 1));
    Details->SetStringField(TEXT("reason"), ScreenshotNameFault(Name));
    Details->SetStringField(TEXT("pattern"), ScreenshotNamePattern);
    Details->SetNumberField(TEXT("max_length"), MaxScreenshotNameLength);
    Details->SetStringField(TEXT("png_path"), TEXT("Saved/Screenshots/<platform>/<filename>_<capture_id>.png"));
    return Details;
}

TSharedPtr<FJsonObject> TimeoutViolation(FString& Message)
{
    TSharedPtr<FJsonObject> Details = RequestViolation(TEXT("timeout_seconds"),
        TEXT("timeout_seconds must be a number from 1 to 120"), Message);
    Details->SetNumberField(TEXT("minimum"), MinCaptureTimeout);
    Details->SetNumberField(TEXT("maximum"), MaxCaptureTimeout);
    return Details;
}

TSharedPtr<FJsonObject> TargetViolation(const TSharedPtr<FJsonValue>& Value, FString& Message)
{
    TSharedPtr<FJsonObject> Details = RequestViolation(TEXT("target"), TEXT("target must be one of level/active/pie"), Message);
    if (Value->Type == EJson::String)
    {
        Details->SetStringField(TEXT("value"), Value->AsString());
    }
    TArray<TSharedPtr<FJsonValue>> Allowed;
    for (const TCHAR* Target : CaptureTargets)
    {
        Allowed.Add(MakeShared<FJsonValueString>(Target));
    }
    Details->SetArrayField(TEXT("allowed"), Allowed);
    return Details;
}

// The schema enumerates exact spellings; FString's == would also admit "PIE".
bool IsCaptureTarget(const FString& Target)
{
    for (const TCHAR* Candidate : CaptureTargets)
    {
        if (Target.Equals(Candidate, ESearchCase::CaseSensitive))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> ReadCaptureFlag(const FJsonObject& Payload, const TCHAR* Field, bool& bValue, FString& Message)
{
    const TSharedPtr<FJsonValue> Value = Payload.TryGetField(Field);
    if (!Value.IsValid())
    {
        return nullptr;
    }
    if (Value->Type != EJson::Boolean)
    {
        return TypeViolation(Field, TEXT("boolean"), Message);
    }
    bValue = Value->AsBool();
    return nullptr;
}
}

bool IsSafeScreenshotName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > MaxScreenshotNameLength)
    {
        return false;
    }
    for (TCHAR Char : Name)
    {
        if (!IsScreenshotNameChar(Char))
        {
            return false;
        }
    }
    return true;
}

TSharedPtr<FJsonObject> CheckNameAndTimeout(const FString& Name, double Timeout, FString& Message)
{
    if (!IsSafeScreenshotName(Name))
    {
        return NameViolation(Name, Message);
    }
    if (!FMath::IsFinite(Timeout) || Timeout < MinCaptureTimeout || Timeout > MaxCaptureTimeout)
    {
        return TimeoutViolation(Message);
    }
    return nullptr;
}

TSharedPtr<FJsonObject> ReadRequest(const FJsonObject& Payload, FRequest& Request, FString& Message)
{
    const TSharedPtr<FJsonValue> Name = Payload.TryGetField(TEXT("filename"));
    if (Name.IsValid() && Name->Type != EJson::Null)
    {
        if (Name->Type != EJson::String)
        {
            return TypeViolation(TEXT("filename"), TEXT("string"), Message);
        }
        Request.Name = Name->AsString();
        if (!IsSafeScreenshotName(Request.Name))
        {
            return NameViolation(Request.Name, Message);
        }
    }
    if (const TSharedPtr<FJsonValue> Target = Payload.TryGetField(TEXT("target")))
    {
        if (Target->Type != EJson::String || !IsCaptureTarget(Target->AsString()))
        {
            return TargetViolation(Target, Message);
        }
        Request.Target = Target->AsString();
    }
    if (const TSharedPtr<FJsonValue> Timeout = Payload.TryGetField(TEXT("timeout_seconds")))
    {
        const double Seconds = Timeout->Type == EJson::Number ? Timeout->AsNumber() : -1;
        if (!FMath::IsFinite(Seconds) || Seconds < MinCaptureTimeout || Seconds > MaxCaptureTimeout)
        {
            return TimeoutViolation(Message);
        }
        Request.Timeout = Seconds;
    }
    if (TSharedPtr<FJsonObject> Violation = ReadCaptureFlag(Payload, TEXT("show_ui"), Request.bShowUi, Message))
    {
        return Violation;
    }
    return ReadCaptureFlag(Payload, TEXT("dry_run"), Request.bDryRun, Message);
}
}
