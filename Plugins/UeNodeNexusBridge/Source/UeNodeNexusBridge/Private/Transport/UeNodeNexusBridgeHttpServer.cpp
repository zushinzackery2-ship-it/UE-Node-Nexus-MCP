#include "UeNodeNexusBridgeHttpServer.h"

#include "Async/Async.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"

DEFINE_LOG_CATEGORY_STATIC(LogUeNodeNexusBridge, Log, All);

namespace
{
static constexpr uint32 BridgePort = 8765;
static const TCHAR* BridgeRoute = TEXT("/mcp");
}

static void CompleteJsonRequest(const FHttpResultCallback& OnComplete, const TSharedPtr<FJsonObject>& Response)
{
    OnComplete(UeNodeNexusBridge::JsonResponse(Response));
}

static void HandleMcpRequestOnGameThread(const FString BodyString, const FHttpResultCallback OnComplete)
{
    UE_LOG(LogUeNodeNexusBridge, Verbose, TEXT("MCP request begin, body_chars=%d"), BodyString.Len());

    TSharedPtr<FJsonObject> RequestJson;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);

    if (!FJsonSerializer::Deserialize(Reader, RequestJson) || !RequestJson.IsValid())
    {
        TSharedPtr<FJsonObject> Response = UeNodeNexusBridge::MakeEnvelope(TEXT("unknown"), TEXT(""), false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_json"), TEXT("Request body is not valid JSON")));
        UE_LOG(LogUeNodeNexusBridge, Warning, TEXT("MCP request invalid_json"));
        CompleteJsonRequest(OnComplete, Response);
        return;
    }

    FString Operation;
    FString RequestId;
    TSharedPtr<FJsonObject> Payload;
    if (!RequestJson->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty() ||
        !RequestJson->TryGetStringField(TEXT("request_id"), RequestId) ||
        !UeNodeNexusBridge::TryGetPayload(RequestJson, Payload))
    {
        TSharedPtr<FJsonObject> Response = UeNodeNexusBridge::MakeEnvelope(Operation.IsEmpty() ? TEXT("unknown") : Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_envelope"), TEXT("Request must include operation, request_id, and payload object")));
        UE_LOG(LogUeNodeNexusBridge, Warning, TEXT("MCP request invalid_envelope operation=%s request_id=%s"), *Operation, *RequestId);
        CompleteJsonRequest(OnComplete, Response);
        return;
    }

    UE_LOG(LogUeNodeNexusBridge, Verbose, TEXT("MCP operation begin operation=%s request_id=%s"), *Operation, *RequestId);
    TSharedPtr<FJsonObject> Response = UeNodeNexusBridge::DispatchOperation(Operation, RequestId, Payload);
    UE_LOG(LogUeNodeNexusBridge, Verbose, TEXT("MCP operation end operation=%s request_id=%s ok=%s"), *Operation, *RequestId, Response.IsValid() && Response->GetBoolField(TEXT("ok")) ? TEXT("true") : TEXT("false"));
    CompleteJsonRequest(OnComplete, Response);
}

static bool HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    const FString BodyString = UeNodeNexusBridge::BodyToString(Request.Body);
    AsyncTask(ENamedThreads::GameThread, [BodyString, OnComplete]()
    {
        HandleMcpRequestOnGameThread(BodyString, OnComplete);
    });
    return true;
}

void FUeNodeNexusBridgeHttpServer::Start()
{
    FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
    Router = HttpServerModule.GetHttpRouter(BridgePort, true);
    if (!Router.IsValid())
    {
        UE_LOG(LogUeNodeNexusBridge, Error, TEXT("Failed to bind HTTP router on port %u"), BridgePort);
        return;
    }

    RouteHandle = Router->BindRoute(FHttpPath(BridgeRoute), EHttpServerRequestVerbs::VERB_POST, FHttpRequestHandler::CreateStatic(&HandleMcpRequest));
    if (!RouteHandle.IsValid())
    {
        UE_LOG(LogUeNodeNexusBridge, Error, TEXT("Failed to bind bridge route %s"), BridgeRoute);
        return;
    }

    HttpServerModule.StartAllListeners();
    UE_LOG(LogUeNodeNexusBridge, Display, TEXT("UE Node Nexus Bridge listening on http://127.0.0.1:%u%s"), BridgePort, BridgeRoute);
}

void FUeNodeNexusBridgeHttpServer::Stop()
{
    if (Router.IsValid() && RouteHandle.IsValid())
    {
        Router->UnbindRoute(RouteHandle);
        RouteHandle.Reset();
    }

    Router.Reset();
}
