// Assertion macro stubs for compiler-only checks; the format arguments are
// intentionally swallowed unchecked. See CoreTypes.h for the rationale.
#pragma once

#define ensureMsgf(InExpression, InFormat, ...) ((void)(InExpression), true)
#define checkf(InExpression, InFormat, ...) ((void)(InExpression))
#define check(InExpression) ((void)(InExpression))
