define_library_target(lesl)

target_compile_definitions(lesl PUBLIC LESL_SPIRV_VERSION=0x10000)

target_sources(lesl PRIVATE
    src/lib.cpp
    src/parser.cpp
    src/stringpool.cpp
)

if (LESL_ENABLE_SDL3)
    target_sources(lesl PRIVATE
        src/sdl.cpp
    )

    target_link_libraries(lesl PUBLIC SDL3::SDL3)
endif()

target_include_directories(lesl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${SPIRV-Headers_SOURCE_DIR}/include)
