// Compiler-only stand-ins for UE core types. These stubs exist so plugin
// translation units written without a UE build environment can be syntax- and
// type-checked with a host clang (tests/test_cpp_compile_check.py). They mirror
// the documented UE 5.5 API shapes used by this plugin and are never linked.
#pragma once

#include <cstdint>
#include <type_traits>

using int32 = std::int32_t;
using uint32 = std::uint32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using TCHAR = wchar_t;

#define UE_STUB_TEXT_PASTE(x) L##x
#define TEXT(x) UE_STUB_TEXT_PASTE(x)

enum
{
    INDEX_NONE = -1
};

namespace ESearchCase
{
enum Type
{
    CaseSensitive,
    IgnoreCase,
};
}

template <typename T>
constexpr std::remove_reference_t<T>&& MoveTemp(T&& Value) noexcept
{
    return static_cast<std::remove_reference_t<T>&&>(Value);
}
