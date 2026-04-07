define_library_target(lesl)

target_compile_definitions(lesl PUBLIC LESL_SPIRV_VERSION=0x10000)

target_sources(lesl PRIVATE
    src/lib.cpp
    src/parser.cpp
    src/stringpool.cpp
)

target_include_directories(lesl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
