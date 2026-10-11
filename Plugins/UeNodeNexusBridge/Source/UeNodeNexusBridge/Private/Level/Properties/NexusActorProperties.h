#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::ActorProperties
{
struct FPath
{
    FProperty* Leaf = nullptr;
    FProperty* Root = nullptr;
    void* Container = nullptr;
};

bool Resolve(UObject* Target, const FString& Name, FPath& Out, FString& Error);
FString Export(UObject* Owner, const FPath& Path);
bool Write(UObject* Owner, const FPath& Path, const TSharedPtr<FJsonValue>& Value, FString& Error);
}
