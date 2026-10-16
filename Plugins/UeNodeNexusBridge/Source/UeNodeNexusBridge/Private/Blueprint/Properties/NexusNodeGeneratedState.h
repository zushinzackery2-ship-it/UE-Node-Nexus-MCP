#pragma once

#include "CoreMinimal.h"

class FProperty;
class UClass;

namespace UeNodeNexusBridge
{
/**
 * Node state the editor maintains itself and rebuilds from the node's inputs.
 *
 * ``UK2Node_MakeStruct`` and its siblings generate ``ShowPinForProperties``
 * when the struct type or pins are reconstructed; the value reflects which
 * struct members are shown as pins, not authored content. Exporting it would
 * make the canonical text carry an editor-made value, and importing it would
 * be overwritten by the next reconstruct, so both sides skip it: text
 * expresses member visibility by connecting members, never by this array.
 */
bool IsGeneratedNodeProperty(const UClass* Class, const FProperty* Property);
}
