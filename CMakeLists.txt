cmake_minimum_required(VERSION 3.16)

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

# Declare the project
project(lesl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

option(LESL_ENABLE_EXAMPLES "Enable examples" ON)
option(LESL_ENABLE_LESLC "Enable the LESL compiler" ON)

cmake_policy(SET CMP0135 NEW)

execute_process(COMMAND git rev-parse HEAD OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)

set(BUILT_TARGETS)

set(LESL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

function(define_library_target target_name)
    add_library(${target_name} STATIC)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_compile_definitions(${target_name} PUBLIC PROJECT_NAME="${target_name}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_COMMIT_HASH="${GIT_COMMIT_HASH}")

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_definitions(${target_name} PUBLIC RELEASE)
    else ()
        target_compile_definitions(${target_name} PUBLIC DEBUG)
    endif()

    if (WIN32)
        target_compile_definitions(${target_name} PUBLIC _CRT_SECURE_NO_WARNINGS)
        target_compile_definitions(${target_name} PUBLIC WIN32_LEAN_AND_MEAN)
    endif()
    if (UNIX)
        target_compile_definitions(${target_name} PUBLIC _POSIX_C_SOURCE=200809L)
        target_link_libraries(${target_name} PRIVATE pthread dl)
    endif()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(${target_name} PRIVATE -static-libstdc++)
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4)
    endif()

    target_include_directories(${target_name} PUBLIC ${LESL_ROOT}/common/include)
    target_sources(${target_name} PRIVATE ${LESL_ROOT}/common/src/log.cpp)

    list(APPEND BUILT_TARGETS ${target_name})
endfunction()

function(define_executable_target target_name)
    add_executable(${target_name})
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    target_compile_definitions(${target_name} PUBLIC PROJECT_NAME="${target_name}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_COMMIT_HASH="${GIT_COMMIT_HASH}")

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_definitions(${target_name} PUBLIC RELEASE)
    else ()
        target_compile_definitions(${target_name} PUBLIC DEBUG)
    endif()

    if (WIN32)
        target_compile_definitions(${target_name} PUBLIC _CRT_SECURE_NO_WARNINGS)
        target_compile_definitions(${target_name} PUBLIC WIN32_LEAN_AND_MEAN)
    endif()
    if (MINGW)
        target_compile_definitions(${target_name} PRIVATE MINGW)
        target_link_options(${target_name} PUBLIC -mwindows)
        target_link_options(${target_name} PUBLIC -static)
    endif()
    if (UNIX)
        target_compile_definitions(${target_name} PUBLIC _POSIX_C_SOURCE=200809L)
        target_link_libraries(${target_name} PRIVATE pthread dl)
    endif()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_libraries(${target_name} PRIVATE -static-libstdc++)
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4)
    endif()

    target_include_directories(${target_name} PUBLIC ${LESL_ROOT}/common/include)
    target_sources(${target_name} PRIVATE ${LESL_ROOT}/common/src/log.cpp)

    list(APPEND BUILT_TARGETS ${target_name})
endfunction()

function(build_spirv_shader shader_name)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/spirv/${shader_name}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND spirv-as -o ${CMAKE_BINARY_DIR}/shaders/spirv/${shader_name}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spirv/${shader_name}.spvasm --target-env vulkan1.0
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spirv/${shader_name}.spvasm
        COMMENT "Building SPIR-V shader ${shader_name}"
    )
endfunction()

function(build_glsl_shader shader_name)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/glsl/${shader_name}.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND glslangValidator -V ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glsl/${shader_name} -o ${CMAKE_BINARY_DIR}/shaders/glsl/${shader_name}.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glsl/${shader_name}
        COMMENT "Building GLSL shader ${shader_name}"
    )
endfunction()

function(validate_spirv_output shader_name shader_spv_file)
    # make sure the validated file is built after the original SPIR-V file (only if the target does not already exist)
    if (NOT TARGET ${shader_name}_validation_target)
        # Validate the generated SPIR-V shader, make sure this runs after the shader is built every time.
        add_custom_command(
            OUTPUT ${shader_name}_validated
            COMMAND spirv-val ${shader_spv_file}
            DEPENDS ${shader_spv_file}
            COMMENT "Validating SPIR-V shader ${shader_spv_file}"
        )
        add_custom_target(${shader_name}_validation_target DEPENDS ${shader_name}_validated)
    endif()
endfunction()

add_subdirectory(lesl)

if (LESL_ENABLE_LESLC)
    add_subdirectory(external/SPIRV-Headers)
    add_subdirectory(leslc)

    if (WIN32)
        set(LESLC ${CMAKE_BINARY_DIR}/Debug/leslc.exe)
    else ()
        set(LESLC ${CMAKE_BINARY_DIR}/Debug/leslc)
    endif()

    function(build_lesl_shader shader_name)
        # Optional arguments: 
        # 0: custom pipeline name
        # 1: custom output name
        set(extra_args ${ARGN})
        set(shader_output_name ${shader_name})
        list(LENGTH extra_args extra_args_length)
        if (extra_args_length GREATER 0)
            list(GET extra_args 0 pipeline_name)
            set(EXTRA_LESLC_ARGS --pipeline ${pipeline_name})
            if (extra_args_length GREATER 1)
                list(GET extra_args 1 shader_output_name)
            endif()
        else ()
            set(EXTRA_LESLC_ARGS "")
        endif()
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/shaders/lesl/${shader_output_name}.spv
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
            COMMAND ${LESLC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lesl/${shader_name}.lesl -o ${CMAKE_BINARY_DIR}/shaders/lesl/${shader_output_name}.spv ${EXTRA_LESLC_ARGS}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lesl/${shader_name}.lesl ${LESLC}
            COMMENT "Building LESL shader ${shader_output_name}"
        )

        validate_spirv_output(${shader_output_name} ${CMAKE_BINARY_DIR}/shaders/lesl/${shader_output_name}.spv)

        set(${shader_output_name}_LESL_SPIRV ${CMAKE_BINARY_DIR}/shaders/lesl/${shader_output_name}.spv PARENT_SCOPE)
    endfunction()
endif()

if (LESL_ENABLE_EXAMPLES)
    set(SDL_SHARED TRUE "Build a SDL shared library (if available)")
    set(SDL_STATIC FALSE "Build a SDL static library (if available)")
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat warnings as errors")
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build assimp tests")
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT FALSE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT FALSE)
    set(ASSIMP_BUILD_GLTF_IMPORTER TRUE)

    include(FetchContent)

    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.2
    )
    FetchContent_MakeAvailable(glm)

    FetchContent_Declare(
        SDL
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.24
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(SDL)

    FetchContent_Declare(
        assimp
        URL https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz
        URL_HASH SHA256=66DFBAEE288F2BC43172440A55D0235DFC7BF885DDA6435C038E8000E79582CB
    )
    FetchContent_MakeAvailable(assimp)

    set_property(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/sdl-src" PROPERTY EXCLUDE_FROM_ALL TRUE)

    add_subdirectory(examples)
endif()

# on Visual Studio, set our app as the default project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${leslc}")

install(TARGETS ${BUILT_TARGETS} DESTINATION ./install)
