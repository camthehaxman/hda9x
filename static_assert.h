// Implements the static_assert macro for pre-C11 compilers

#pragma once

#ifndef static_assert
#define static_assert(cond, msg) typedef char static_assertion_##msg[(cond)?1:-1]
#endif
