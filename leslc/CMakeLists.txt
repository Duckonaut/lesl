define_executable_target(leslc)

target_sources(leslc PRIVATE
    src/main.cpp
)

target_include_directories(leslc PUBLIC ${SPIRV-Headers_SOURCE_DIR}/include)
