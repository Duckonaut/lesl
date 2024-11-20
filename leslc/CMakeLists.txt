define_executable_target(leslc)

target_sources(leslc PRIVATE
    src/main.cpp
    src/stringpool.cpp
    src/parser.cpp
)

target_include_directories(leslc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
