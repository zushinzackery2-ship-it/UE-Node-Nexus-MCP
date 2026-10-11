#include "NexusPipeConnection.h"

#include "Containers/StringConv.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNamedPipeIo.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusPipeConnection, Log, All);

namespace UeNodeNexusBridge
{
static constexpr uint32 MaxFrameBytes = 64u * 1024u * 1024u;

bool ServeNamedPipeConnection(void* Pipe, void* IoEvent, void* StopEventHandle, FThreadSafeBool& Stopping)
{
    // Frame = 4-byte little-endian length + UTF-8 JSON body.
    uint8 LengthBytes[4];
    if (!UeNodeNexusBridge::ReadNamedPipeExact(
        Pipe,
        IoEvent,
        StopEventHandle,
        Stopping,
        LengthBytes,
        4))
    {
        return false;
    }
    const uint32 BodyLength =
        static_cast<uint32>(LengthBytes[0]) |
        (static_cast<uint32>(LengthBytes[1]) << 8) |
        (static_cast<uint32>(LengthBytes[2]) << 16) |
        (static_cast<uint32>(LengthBytes[3]) << 24);

    if (BodyLength == 0 || BodyLength > MaxFrameBytes)
    {
        UE_LOG(LogNexusPipeConnection, Warning, TEXT("Named pipe rejected frame length %u"), BodyLength);
        return false;
    }

    TArray<uint8> BodyBytes;
    BodyBytes.SetNumUninitialized(static_cast<int32>(BodyLength));
    if (!UeNodeNexusBridge::ReadNamedPipeExact(
        Pipe,
        IoEvent,
        StopEventHandle,
        Stopping,
        BodyBytes.GetData(),
        BodyLength))
    {
        return false;
    }

    const FString Body = UeNodeNexusBridge::BodyToString(BodyBytes);

    FString ResponseString;
    if (!UeNodeNexusBridge::DispatchNamedPipeRequest(Body, Pipe, Stopping, ResponseString))
    {
        return false;   // stopping mid-flight
    }

    const FTCHARToUTF8 Utf8(*ResponseString);
    const uint32 RespLength = static_cast<uint32>(Utf8.Length());
    uint8 RespLengthBytes[4];
    RespLengthBytes[0] = static_cast<uint8>(RespLength & 0xFF);
    RespLengthBytes[1] = static_cast<uint8>((RespLength >> 8) & 0xFF);
    RespLengthBytes[2] = static_cast<uint8>((RespLength >> 16) & 0xFF);
    RespLengthBytes[3] = static_cast<uint8>((RespLength >> 24) & 0xFF);

    if (!UeNodeNexusBridge::WriteNamedPipeAll(
        Pipe,
        IoEvent,
        StopEventHandle,
        Stopping,
        RespLengthBytes,
        4))
    {
        return false;
    }
    if (RespLength > 0
        && !UeNodeNexusBridge::WriteNamedPipeAll(
            Pipe,
            IoEvent,
            StopEventHandle,
            Stopping,
            reinterpret_cast<const uint8*>(Utf8.Get()),
            RespLength))
    {
        return false;
    }
    return true;
}
}
