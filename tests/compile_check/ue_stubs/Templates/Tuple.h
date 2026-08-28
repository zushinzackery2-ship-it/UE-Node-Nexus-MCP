// TPair stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include <utility>

template <typename KeyType, typename ValueType>
struct TPair
{
    TPair() = default;

    template <typename KeyArg, typename ValueArg>
    TPair(KeyArg&& InKey, ValueArg&& InValue)
        : Key(std::forward<KeyArg>(InKey))
        , Value(std::forward<ValueArg>(InValue))
    {
    }

    KeyType Key{};
    ValueType Value{};
};
