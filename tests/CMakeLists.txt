define_executable_target(lesl-benchmark)

target_sources(lesl-benchmark PRIVATE
    benchmark/main.cpp
)

target_link_libraries(lesl-benchmark PRIVATE lesl)

target_include_directories(leslc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
