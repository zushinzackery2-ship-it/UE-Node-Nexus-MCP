#include "NexusCapture.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

namespace UeNodeNexusBridge::Capture
{
FViewport* ResolveViewport(const FString& Target, FString& Resolved)
{
    if (GEditor == nullptr)
    {
        return nullptr;
    }
    if (Target == TEXT("active"))
    {
        Resolved = TEXT("active");
        return GEditor->GetActiveViewport();
    }
    if (Target == TEXT("pie") && GEditor->PlayWorld && GEngine && GEngine->GameViewport)
    {
        Resolved = TEXT("pie");
        return GEngine->GameViewport->Viewport;
    }
    if (Target != TEXT("level"))
    {
        return nullptr;
    }
    if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
    {
        Resolved = TEXT("level_current");
        return GCurrentLevelEditingViewportClient->Viewport;
    }
    for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
    {
        if (Client && Client->Viewport && Client->IsPerspective())
        {
            Resolved = TEXT("level_perspective");
            return Client->Viewport;
        }
    }
    return nullptr;
}
}
