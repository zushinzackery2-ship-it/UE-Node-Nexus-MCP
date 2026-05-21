#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"

class IHttpRouter;

class FUeNodeNexusBridgeHttpServer
{
public:
    void Start();
    void Stop();

private:
    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle RouteHandle;
};

