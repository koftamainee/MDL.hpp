cmake_minimum_required(VERSION 3.25)

project(MDL VERSION 0.1.0 LANGUAGES CXX)

include(CMake/Options.cmake)
include(CMake/CompilerFlags.cmake)


add_library(MDL STATIC
    Source/Private/Arena.cpp
    Source/Private/Error.cpp
    Source/Private/Parser.cpp
    Source/Private/Value.cpp
    Source/Private/Writer.cpp
)
add_library(MDL::MDL ALIAS MDL)

target_compile_features(MDL PUBLIC cxx_std_23)
set_target_properties(MDL PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_COMPILE_COMMANDS ON
)

target_include_directories(MDL
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source/Public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Private
)

target_compile_options(MDL PRIVATE ${MDL_COMPILE_FLAGS})
target_compile_definitions(MDL
    PRIVATE ${MDL_PRIVATE_DEFINITIONS}
    PUBLIC
        MDL_MAX_DEPTH=${MDL_MAX_DEPTH}
        MDL_SNAKE_CASE_NAMES=$<BOOL:${MDL_SNAKE_CASE_NAMES}>
        MDL_ENUM_MIN=(${MDL_ENUM_MIN})
        MDL_ENUM_MAX=(${MDL_ENUM_MAX})
        MDL_ENABLE_ASSERTS=$<BOOL:${MDL_ENABLE_ASSERTS}>
)

mdl_print_options()


if (MDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if (MDL_BUILD_BENCHMARKS)
    add_subdirectory(Bench)
endif()
