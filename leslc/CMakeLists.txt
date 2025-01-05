define_executable_target(leslc)

target_sources(leslc PRIVATE
    src/main.cpp
    src/stringpool.cpp
    src/parser.cpp
    ${LESL_ROOT}/tools/lesl_repr.natvis
)

target_include_directories(leslc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)

define_executable_target(spvgen_demo)

target_sources(spvgen_demo PRIVATE
    src/spvgen_demo.cpp
)

target_include_directories(spvgen_demo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
