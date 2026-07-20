define_library_target(lesl)

target_compile_definitions(lesl PUBLIC LESL_SPIRV_VERSION=0x10300)

target_sources(lesl PRIVATE
    src/lib.cpp
    src/parser.cpp
    src/stringpool.cpp
    ../external/json/jsoncpp.cpp
)

if (LESL_ENABLE_SDL3)
    target_sources(lesl PRIVATE
        src/sdl.cpp
    )

    target_link_libraries(lesl PUBLIC SDL3::SDL3)
endif()

if (LESL_ENABLE_OPT)
    target_compile_definitions(lesl PUBLIC LESL_ENABLE_OPT)
    target_link_libraries(lesl PUBLIC "SPIRV-Tools-static" "SPIRV-Tools-opt")
endif()

target_include_directories(lesl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SPIRV-Headers_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/
)

