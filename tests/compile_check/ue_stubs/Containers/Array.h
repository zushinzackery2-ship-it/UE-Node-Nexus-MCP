// TArray stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include "CoreTypes.h"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <vector>

template <typename ElementType>
class TArray
{
public:
    TArray() = default;
    TArray(std::initializer_list<ElementType> Init) : Storage(Init) {}

    int32 Num() const { return static_cast<int32>(Storage.size()); }
    bool IsEmpty() const { return Storage.empty(); }

    int32 Add(ElementType Item)
    {
        Storage.push_back(std::move(Item));
        return Num() - 1;
    }

    template <typename... ArgTypes>
    int32 Emplace(ArgTypes&&... Args)
    {
        Storage.emplace_back(std::forward<ArgTypes>(Args)...);
        return Num() - 1;
    }

    ElementType& operator[](int32 Index) { return Storage[static_cast<std::size_t>(Index)]; }
    const ElementType& operator[](int32 Index) const { return Storage[static_cast<std::size_t>(Index)]; }

    template <typename PredicateType>
    void Sort(PredicateType Predicate)
    {
        std::sort(Storage.begin(), Storage.end(), Predicate);
    }

    ElementType* begin() { return Storage.data(); }
    ElementType* end() { return Storage.data() + Storage.size(); }
    const ElementType* begin() const { return Storage.data(); }
    const ElementType* end() const { return Storage.data() + Storage.size(); }

private:
    std::vector<ElementType> Storage;
};
