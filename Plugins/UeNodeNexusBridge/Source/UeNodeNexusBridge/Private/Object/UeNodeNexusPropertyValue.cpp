#include "UeNodeNexusPropertyValue.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool ConvertPropertyJsonValue(UObject* Object, FProperty* Property, void* Storage,
    const TSharedPtr<FJsonValue>& Value, FString& Text, FString& Error);

FPropertyValueBuffer::FPropertyValueBuffer(UObject* InOwner, FProperty* InProperty)
    : Owner(InOwner), Property(InProperty)
{
    Storage = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Storage);
    Property->CopyCompleteValue(Storage, Property->ContainerPtrToValuePtr<void>(Owner));
}

FPropertyValueBuffer::~FPropertyValueBuffer()
{
    Property->DestroyValue(Storage);
    FMemory::Free(Storage);
}

bool FPropertyValueBuffer::ApplyJson(const TSharedPtr<FJsonValue>& Value, FString& Text, FString& Error)
{
    return ConvertPropertyJsonValue(Owner, Property, Storage, Value, Text, Error);
}

bool FPropertyValueBuffer::ApplyText(const FString& Text, FString& Error)
{
    if (Property->ImportText_Direct(*Text, Storage, Owner, PPF_None) == nullptr)
    {
        Error = TEXT("property_import_failed");
        return false;
    }
    return true;
}

bool FPropertyValueBuffer::Changed() const
{
    return !Property->Identical(Storage, Property->ContainerPtrToValuePtr<void>(Owner), PPF_None);
}

void FPropertyValueBuffer::Commit() const
{
    Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Owner), Storage);
}
}
