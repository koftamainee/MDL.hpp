# Flags used to build the MDL library itself (not forced on consumers).
# MDL is built without exceptions and RTTI, it never throws.

if (MSVC)
    set(MDL_COMPILE_FLAGS /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /GR- /EHs-c-)
    set(MDL_PRIVATE_DEFINITIONS _HAS_EXCEPTIONS=0 NOMINMAX)
    if (MDL_WARNINGS_AS_ERRORS)
        list(APPEND MDL_COMPILE_FLAGS /WX)
    endif()
else()
    set(MDL_COMPILE_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers -fno-exceptions -fno-rtti)
    set(MDL_PRIVATE_DEFINITIONS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND MDL_COMPILE_FLAGS -Wno-missing-designated-field-initializers)
    endif()
    if (MDL_WARNINGS_AS_ERRORS)
        list(APPEND MDL_COMPILE_FLAGS -Werror)
    endif()
endif()
