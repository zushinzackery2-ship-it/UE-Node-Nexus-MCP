#pragma once

#include "CoreMinimal.h"

class FProperty;
class FJsonValue;
class UObject;

namespace UeNodeNexusBridge
{
// Owns an initialized copy; conversion cannot mutate the live UObject.
class UENODENEXUSBRIDGE_API FPropertyValueBuffer
{
public:
    FPropertyValueBuffer(UObject* InOwner, FProperty* InProperty);
    ~FPropertyValueBuffer();
    FPropertyValueBuffer(const FPropertyValueBuffer&) = delete;
    FPropertyValueBuffer& operator=(const FPropertyValueBuffer&) = delete;
    bool ApplyJson(const TSharedPtr<FJsonValue>& Value, FString& Text, FString& Error);
    bool ApplyText(const FString& Text, FString& Error);
    bool Changed() const;
    void Commit() const;
private:
    UObject* Owner;
    FProperty* Property;
    void* Storage;
};
}
