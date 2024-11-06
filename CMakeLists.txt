cmake_minimum_required(VERSION 3.16)

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

# Declare the project
project(lesl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

option(ENABLE_EXAMPLES "Enable examples" ON)

cmake_policy(SET CMP0135 NEW)

execute_process(COMMAND git rev-parse HEAD OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)

set(BUILT_TARGETS)

set(LESL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

function(define_executable_target target_name)
    add_executable(${target_name})
    target_link_libraries(${target_name} PRIVATE -static-libgcc -static-libstdc++)
    target_compile_features(${target_name} PRIVATE cxx_std_20 c_std_11)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(${target_name} PUBLIC PROJECT_NAME="${target_name}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")
    target_compile_definitions(${target_name} PUBLIC PROJECT_COMMIT_HASH="${GIT_COMMIT_HASH}")

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_definitions(${target_name} PUBLIC RELEASE)
        target_compile_options(${target_name} PRIVATE -O3)
    else ()
        target_compile_definitions(${target_name} PUBLIC DEBUG)
        target_compile_options(${target_name} PRIVATE -O1 -g)
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

    target_include_directories(${target_name} PUBLIC ${LESL_ROOT}/common/include)
    target_sources(${target_name} PRIVATE ${LESL_ROOT}/common/src/log.cpp)

    list(APPEND BUILT_TARGETS ${target_name})
endfunction()

# fetch dependencies

if (ENABLE_EXAMPLES)
    set(SDL_SHARED TRUE "Build a SDL shared library (if available)")
    set(SDL_STATIC FALSE "Build a SDL static library (if available)")

    include(FetchContent)

    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG bf71a834948186f4097caa076cd2663c69a10e1e #refs/tags/1.0.1
    )
    FetchContent_MakeAvailable(glm)

    FetchContent_Declare(
        SDL
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(SDL)

    set_property(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps/sdl-src" PROPERTY EXCLUDE_FROM_ALL TRUE)

    add_subdirectory(examples)
endif()

add_subdirectory(lesl)
add_subdirectory(leslc)

# on Visual Studio, set our app as the default project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "${leslc}")

install(TARGETS ${BUILT_TARGETS} DESTINATION ./install)
