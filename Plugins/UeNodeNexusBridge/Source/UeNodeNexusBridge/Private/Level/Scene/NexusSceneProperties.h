#pragma once

#include "NexusSceneJson.h"

namespace UeNodeNexusBridge::Scene
{
bool EditableProperty(const FProperty* Property);
void ExportProperties(UObject* Target, FObject& Properties, FObject& Defaults, FObject& Schema);
bool WriteProperties(UObject* Target, const FObject& Properties, bool bApply, FString& Error, bool bNotify = true);
bool GatherPropertyReferences(UObject* Target, const FObject& Properties, TSet<UObject*>& References, FString& Error);
}
