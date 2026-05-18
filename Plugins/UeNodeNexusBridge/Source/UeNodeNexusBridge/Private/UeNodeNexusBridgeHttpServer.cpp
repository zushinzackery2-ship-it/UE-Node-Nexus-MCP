#include "UeNodeNexusBridgeHttpServer.h"

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

static bool HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    const FString BodyString = UeNodeNexusBridge::BodyToString(Request.Body);
    TSharedPtr<FJsonObject> RequestJson;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);

    if (!FJsonSerializer::Deserialize(Reader, RequestJson) || !RequestJson.IsValid())
    {
        TSharedPtr<FJsonObject> Response = UeNodeNexusBridge::MakeEnvelope(TEXT("unknown"), TEXT(""), false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_json"), TEXT("Request body is not valid JSON")));
        OnComplete(UeNodeNexusBridge::JsonResponse(Response));
        return true;
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
        OnComplete(UeNodeNexusBridge::JsonResponse(Response));
        return true;
    }

    OnComplete(UeNodeNexusBridge::JsonResponse(UeNodeNexusBridge::DispatchOperation(Operation, RequestId, Payload)));
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

