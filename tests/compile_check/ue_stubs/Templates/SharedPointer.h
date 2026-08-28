// TSharedPtr / TSharedRef / MakeShared stubs backed by std::shared_ptr so
// pointer conversions genuinely type-check; see CoreTypes.h for the rationale.
#pragma once

#include <memory>
#include <utility>

template <typename ObjectType>
class TSharedRef;

template <typename ObjectType>
class TSharedPtr
{
public:
    TSharedPtr() = default;
    TSharedPtr(decltype(nullptr)) {}

    template <typename OtherType>
    TSharedPtr(const TSharedPtr<OtherType>& Other) : Storage(Other.Storage)
    {
    }

    template <typename OtherType>
    TSharedPtr(const TSharedRef<OtherType>& Other);

    bool IsValid() const { return static_cast<bool>(Storage); }
    ObjectType* Get() const { return Storage.get(); }
    ObjectType* operator->() const { return Storage.get(); }
    ObjectType& operator*() const { return *Storage; }
    explicit operator bool() const { return IsValid(); }
    void Reset() { Storage.reset(); }

    // Public so converting constructors across instantiations can read it.
    std::shared_ptr<ObjectType> Storage;
};

template <typename ObjectType>
class TSharedRef
{
public:
    explicit TSharedRef(std::shared_ptr<ObjectType> InStorage) : Storage(std::move(InStorage)) {}

    template <typename OtherType>
    TSharedRef(const TSharedRef<OtherType>& Other) : Storage(Other.Storage)
    {
    }

    ObjectType* operator->() const { return Storage.get(); }
    ObjectType& operator*() const { return *Storage; }
    ObjectType& Get() const { return *Storage; }

    std::shared_ptr<ObjectType> Storage;
};

template <typename ObjectType>
template <typename OtherType>
TSharedPtr<ObjectType>::TSharedPtr(const TSharedRef<OtherType>& Other) : Storage(Other.Storage)
{
}

template <typename ObjectType, typename... ArgTypes>
TSharedRef<ObjectType> MakeShared(ArgTypes&&... Args)
{
    return TSharedRef<ObjectType>(std::make_shared<ObjectType>(std::forward<ArgTypes>(Args)...));
}
