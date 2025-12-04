define_executable_target(leslc)

target_sources(leslc PRIVATE
    src/main.cpp
)

target_link_libraries(leslc PRIVATE lesl)

target_include_directories(leslc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
