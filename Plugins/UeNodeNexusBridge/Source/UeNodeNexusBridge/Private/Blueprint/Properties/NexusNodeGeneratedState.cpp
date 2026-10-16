#include "NexusNodeGeneratedState.h"

#include "K2Node_StructOperation.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool IsGeneratedNodeProperty(const UClass* Class, const FProperty* Property)
{
    if (Class == nullptr || Property == nullptr)
    {
        return false;
    }
    // Struct-operation nodes are the first family whose construction rewrites a
    // stored editor array; extend the check here when another appears.
    if (Class->IsChildOf(UK2Node_StructOperation::StaticClass())
        && Property->GetFName() == TEXT("ShowPinForProperties"))
    {
        return true;
    }
    return false;
}
}
