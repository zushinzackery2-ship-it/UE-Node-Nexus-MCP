#include "UeNodeNexusBridgeOperations.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

// viewport_camera_get / viewport_camera_set: read and move the level editor
// viewport camera. Without this a screenshot shows whatever the map's saved
// camera happened to look at (after an editor restart, a rain-glass panel
// instead of the character), and "does the highlight follow the camera" cannot
// be answered because there is no way to take the second angle.
namespace UeNodeNexusBridge
{
namespace
{
bool ReadNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Lower, const TCHAR* Upper, double& OutValue)
{
    return Json->TryGetNumberField(Lower, OutValue) || Json->TryGetNumberField(Upper, OutValue);
}

bool ReadVector(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, FVector& OutValue)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Payload->TryGetObjectField(Field, Object) || Object == nullptr || !Object->IsValid())
    {
        return false;
    }
    double X = 0.0, Y = 0.0, Z = 0.0;
    ReadNumber(*Object, TEXT("x"), TEXT("X"), X);
    ReadNumber(*Object, TEXT("y"), TEXT("Y"), Y);
    ReadNumber(*Object, TEXT("z"), TEXT("Z"), Z);
    OutValue = FVector(X, Y, Z);
    return true;
}

bool ReadRotator(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, FRotator& OutValue)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Payload->TryGetObjectField(Field, Object) || Object == nullptr || !Object->IsValid())
    {
        return false;
    }
    double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
    ReadNumber(*Object, TEXT("pitch"), TEXT("Pitch"), Pitch);
    ReadNumber(*Object, TEXT("yaw"), TEXT("Yaw"), Yaw);
    ReadNumber(*Object, TEXT("roll"), TEXT("Roll"), Roll);
    OutValue = FRotator(Pitch, Yaw, Roll);
    return true;
}

// The viewport the user looks through, or the first perspective level viewport.
// Orthographic viewports have no meaningful location/rotation pair to write.
FLevelEditorViewportClient* ResolveLevelViewportClient(FString& OutResolved)
{
    if (GEditor == nullptr)
    {
        return nullptr;
    }
    FLevelEditorViewportClient* Current = GCurrentLevelEditingViewportClient;
    if (Current != nullptr && Current->IsPerspective())
    {
        OutResolved = TEXT("level_current");
        return Current;
    }
    for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
    {
        if (Client != nullptr && Client->IsPerspective())
        {
            OutResolved = TEXT("level_perspective");
            return Client;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> CameraToJson(FLevelEditorViewportClient* Client)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetObjectField(TEXT("location"), MakeVectorJson(Client->GetViewLocation()));
    Json->SetObjectField(TEXT("rotation"), MakeRotatorJson(Client->GetViewRotation()));
    Json->SetNumberField(TEXT("fov"), Client->ViewFOV);
    Json->SetBoolField(TEXT("realtime"), Client->IsRealtime());
    return Json;
}
}

TSharedPtr<FJsonObject> HandleViewportCameraGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Resolved;
    FLevelEditorViewportClient* Client = ResolveLevelViewportClient(Resolved);
    if (Client == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("viewport_unavailable"), TEXT("No perspective level editor viewport is available"));
    }
    TSharedPtr<FJsonObject> Data = CameraToJson(Client);
    Data->SetStringField(TEXT("target"), Resolved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleViewportCameraSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Resolved;
    FLevelEditorViewportClient* Client = ResolveLevelViewportClient(Resolved);
    if (Client == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("viewport_unavailable"), TEXT("No perspective level editor viewport is available"));
    }

    FVector Location = Client->GetViewLocation();
    const bool bHasLocation = ReadVector(Payload, TEXT("location"), Location);
    FRotator Rotation = Client->GetViewRotation();
    const bool bHasRotation = ReadRotator(Payload, TEXT("rotation"), Rotation);
    FVector LookAt = FVector::ZeroVector;
    const bool bHasLookAt = ReadVector(Payload, TEXT("look_at"), LookAt);
    double Fov = 0.0;
    const bool bHasFov = Payload->TryGetNumberField(TEXT("fov"), Fov);
    if (!bHasLocation && !bHasRotation && !bHasLookAt && !bHasFov)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("provide at least one of location / rotation / look_at / fov"));
    }
    if (bHasRotation && bHasLookAt)
    {
        return MakeOperationError(Operation, RequestId, TEXT("target_conflict"), TEXT("rotation and look_at both set the view direction; pass one"));
    }
    if (bHasFov && (Fov <= 1.0 || Fov >= 170.0))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("fov must be in (1, 170) degrees"));
    }
    if (bHasLookAt)
    {
        // Aim from the (possibly new) location; a zero-length direction has no rotation.
        const FVector Direction = LookAt - Location;
        if (Direction.IsNearlyZero())
        {
            return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("look_at coincides with the camera location"));
        }
        Rotation = Direction.Rotation();
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("target"), Resolved);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetObjectField(TEXT("before"), CameraToJson(Client));
    if (!bDryRun)
    {
        if (bHasLocation || bHasLookAt)
        {
            Client->SetViewLocation(Location);
        }
        if (bHasRotation || bHasLookAt)
        {
            Client->SetViewRotation(Rotation);
        }
        if (bHasFov)
        {
            Client->ViewFOV = static_cast<float>(Fov);
        }
        Client->Invalidate();
    }
    Data->SetObjectField(TEXT("after"), CameraToJson(Client));
    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Planned = MakeShared<FJsonObject>();
        Planned->SetObjectField(TEXT("location"), MakeVectorJson(Location));
        Planned->SetObjectField(TEXT("rotation"), MakeRotatorJson(Rotation));
        Planned->SetNumberField(TEXT("fov"), bHasFov ? Fov : static_cast<double>(Client->ViewFOV));
        Data->SetObjectField(TEXT("planned"), Planned);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
