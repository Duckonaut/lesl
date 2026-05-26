define_executable_target(lesl-benchmark)

target_sources(lesl-benchmark PRIVATE
    benchmark/main.cpp
)

target_link_libraries(lesl-benchmark PRIVATE lesl)

set(ALLOW_EXTERNAL_SPIRV_TOOLS ON)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 16.3.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(glslang)

define_executable_target(lesl-glslang-comparer)

target_sources(lesl-glslang-comparer PRIVATE
    glslang-comparison/glslang-comparer.cpp
)

target_link_libraries(lesl-glslang-comparer PRIVATE lesl glslang glslang-default-resource-limits)

function(define_test target_name target_group)
    define_executable_target("test-${target_group}-${target_name}")
    target_link_libraries("test-${target_group}-${target_name}" PRIVATE gtest_main lesl)
    gtest_discover_tests("test-${target_group}-${target_name}")
endfunction()

define_test(basic captures)
target_sources(test-captures-basic PRIVATE captures/basic.cpp)


define_test(basic compilation)
target_sources(test-compilation-basic PRIVATE compilation/basic.cpp)
target_link_libraries(test-compilation-basic PRIVATE "SPIRV-Tools-static")
