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
