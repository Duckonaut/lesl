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
endfunction()

define_sdl3_example(triangle)

build_lesl_shader(triangle)

target_sources(sdl3-triangle PRIVATE
    ${triangle_LESL_SPIRV}
)

define_sdl3_example(cube)

build_lesl_shader(cube)

target_sources(sdl3-cube PRIVATE
    ${cube_LESL_SPIRV}
)

define_sdl3_example(matrices)

build_lesl_shader(matrices)

target_sources(sdl3-matrices PRIVATE
    ${matrices_LESL_SPIRV}
)

define_sdl3_example(swizzles)

build_lesl_shader(swizzles)

target_sources(sdl3-swizzles PRIVATE
    ${swizzles_LESL_SPIRV}
)
