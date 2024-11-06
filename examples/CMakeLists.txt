function(define_sdl3_example example-name)
    define_executable_target(sdl3-${example-name})

    target_sources(sdl3-${example-name} PRIVATE
        sdl3/common.cpp
        sdl3/${example-name}.cpp
    )

    target_link_libraries(sdl3-${example-name} PUBLIC
        glm::glm
        SDL3::SDL3
    )

    target_compile_definitions(sdl3-${example-name} PUBLIC SDL_MAIN_USE_CALLBACKS)

    target_compile_options(sdl3-${example-name} PRIVATE -Wno-unused-parameter -Wno-missing-field-initializers)
endfunction()

define_sdl3_example(triangle)

build_spirv_shader(unified_basic)

target_sources(sdl3-triangle PRIVATE
    ${CMAKE_BINARY_DIR}/shaders/spirv/unified_basic.spv
)
