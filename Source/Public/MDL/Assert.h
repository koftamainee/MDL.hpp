#pragma once

#include <MDL/Config.h>


namespace MDL::Detail {

[[noreturn]] void AssertFailed(const char *expr, const char *file, int line);

} // namespace MDL::Detail


#if MDL_ENABLE_ASSERTS

#define MDL_CHECK(expr)                                             \
    do {                                                            \
        if (!(expr)) {                                              \
            ::MDL::Detail::AssertFailed(#expr, __FILE__, __LINE__); \
        }                                                           \
    } while (0)

#else

#define MDL_CHECK(expr) ((void)0)

#endif
