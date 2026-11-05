#pragma once

// All of these are normally set from CMake (see MDL_* options in CMakeLists.txt).

// Maximum nesting of objects/lists for the parser and the writer.
#ifndef MDL_MAX_DEPTH
#define MDL_MAX_DEPTH 128
#endif

// Default naming for reflected field and enumerator names:
// 1 - CamelCase identifiers map to snake_case keys (DepthTest -> depth_test)
// 0 - names are used as is
#ifndef MDL_SNAKE_CASE_NAMES
#define MDL_SNAKE_CASE_NAMES 1
#endif

// Range of enum values scanned by enum reflection. Can be overridden per enum
// through MDL::Reflect<E>::EnumMin / EnumMax.
#ifndef MDL_ENUM_MIN
#define MDL_ENUM_MIN (-16)
#endif

#ifndef MDL_ENUM_MAX
#define MDL_ENUM_MAX 127
#endif

#ifndef MDL_ENABLE_ASSERTS
#define MDL_ENABLE_ASSERTS 0
#endif
