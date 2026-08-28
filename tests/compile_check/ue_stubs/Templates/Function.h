// TFunction stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include <functional>

template <typename FuncType>
using TFunction = std::function<FuncType>;
