# copy assets to same directory as the executable
add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
)

add_custom_target(copy_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
)

function(define_sdl3_example example-name)
    define_executable_target(sdl3-${example-name})

    target_sources(sdl3-${example-name} PRIVATE
        sdl3/common.cpp
        sdl3/${example-name}.cpp
    )

    target_link_libraries(sdl3-${example-name} PUBLIC
        glm::glm
        SDL3::SDL3
        assimp
        lesl
    )

    target_include_directories(sdl3-${example-name} PUBLIC
        ../external/stb/
    )

    target_compile_definitions(sdl3-${example-name} PUBLIC SDL_MAIN_USE_CALLBACKS)

    add_dependencies(sdl3-${example-name} copy_assets copy_shaders)
endfunction()

define_sdl3_example(triangle)

build_lesl_shader(triangle)
build_lesl_shader(unistruct)

target_sources(sdl3-triangle PRIVATE
    ${triangle_LESL_SPIRV}
    ${unistruct_LESL_SPIRV}
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

define_sdl3_example(multipipeline)

build_lesl_shader(multipipeline A multipipeline_a)
build_lesl_shader(multipipeline B multipipeline_b)

target_sources(sdl3-multipipeline PRIVATE
    ${multipipeline_a_LESL_SPIRV}
    ${multipipeline_b_LESL_SPIRV}
)

define_sdl3_example(time)

build_lesl_shader(time)

target_sources(sdl3-time PRIVATE
    ${time_LESL_SPIRV}
)

define_sdl3_example(texture)

build_lesl_shader(texture)

target_sources(sdl3-texture PRIVATE
    ${texture_LESL_SPIRV}
)

define_sdl3_example(if)

build_lesl_shader(if)

target_sources(sdl3-if PRIVATE
    ${if_LESL_SPIRV}
)

define_sdl3_example(outline)

build_lesl_shader(outline)

target_sources(sdl3-outline PRIVATE
    ${outline_LESL_SPIRV}
)

define_sdl3_example(pbr)

build_lesl_shader(pbr_gbuffer)
build_lesl_shader(pbr_lighting)

target_sources(sdl3-pbr PRIVATE
    ${pbr_gbuffer_LESL_SPIRV}
    ${pbr_lighting_LESL_SPIRV}
)

target_link_libraries(sdl3-pbr PUBLIC
    assimp
)
