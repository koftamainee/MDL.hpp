include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${MDL_GOOGLETEST_TAG}
    GIT_SHALLOW TRUE
    SYSTEM
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


add_executable(MDLTests
    AllocatorTests.cpp
    ParserTests.cpp
    ReflectionTests.cpp
    ValueTests.cpp
    WriterTests.cpp
)

set_target_properties(MDLTests PROPERTIES CXX_EXTENSIONS OFF)

# same flags as the library: no exceptions, no RTTI
target_compile_options(MDLTests PRIVATE ${MDL_COMPILE_FLAGS})
target_compile_definitions(MDLTests PRIVATE
    ${MDL_PRIVATE_DEFINITIONS}
    MDL_TEST_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/Assets"
)
target_link_libraries(MDLTests PRIVATE MDL::MDL GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(MDLTests)
