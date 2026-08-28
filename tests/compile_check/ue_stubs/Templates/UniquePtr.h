// TUniquePtr stub for compiler-only checks; see CoreTypes.h for the rationale.
#pragma once

#include <memory>

template <typename ObjectType>
using TUniquePtr = std::unique_ptr<ObjectType>;
