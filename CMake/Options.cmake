# All MDL configuration lives here. Set these before add_subdirectory() /
# FetchContent_MakeAvailable() to configure MDL from a parent project.

option(MDL_BUILD_TESTS "Build MDL tests (GoogleTest)" ${PROJECT_IS_TOP_LEVEL})
option(MDL_BUILD_BENCHMARKS "Build MDL benchmarks" OFF)
option(MDL_WARNINGS_AS_ERRORS "Treat warnings as errors when building MDL" ${PROJECT_IS_TOP_LEVEL})

option(MDL_SNAKE_CASE_NAMES
    "Reflection maps CamelCase field/enumerator names to snake_case keys" ON)

set(MDL_MAX_DEPTH 128 CACHE STRING "Maximum nesting depth for the parser and writer")
set(MDL_ENUM_MIN -16 CACHE STRING "Lowest enum value scanned by enum reflection")
set(MDL_ENUM_MAX 127 CACHE STRING "Highest enum value scanned by enum reflection")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MDL_ASSERTS_DEFAULT ON)
else()
    set(MDL_ASSERTS_DEFAULT OFF)
endif()
option(MDL_ENABLE_ASSERTS "Enable internal MDL checks" ${MDL_ASSERTS_DEFAULT})

# GoogleTest is fetched only when tests are built. Point this to a local
# checkout to build offline (FETCHCONTENT_SOURCE_DIR_GOOGLETEST works too).
set(MDL_GOOGLETEST_TAG v1.17.0 CACHE STRING "GoogleTest version used by MDL tests")


function(mdl_print_options)
    message(STATUS "[MDL] version          ${PROJECT_VERSION}")
    message(STATUS "[MDL] tests            ${MDL_BUILD_TESTS}")
    message(STATUS "[MDL] benchmarks       ${MDL_BUILD_BENCHMARKS}")
    message(STATUS "[MDL] snake_case names ${MDL_SNAKE_CASE_NAMES}")
    message(STATUS "[MDL] max depth        ${MDL_MAX_DEPTH}")
    message(STATUS "[MDL] enum range       [${MDL_ENUM_MIN}, ${MDL_ENUM_MAX}]")
    message(STATUS "[MDL] asserts          ${MDL_ENABLE_ASSERTS}")
endfunction()
