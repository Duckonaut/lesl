#include "lesl/sdl.hpp"
#include "gtest/gtest.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL.h>

#include <lesl/lesl.hpp>

#include "compilation_utils.hpp"

TEST(Compilation, White) {
    static const char* white_shader = "struct VsIn { float4 p }"
                                      "struct FsOut { float4 c }"
                                      "function vertex (VsIn v) -> () {"
                                      "    POSITION = v.p"
                                      "}"
                                      "function fragment () -> (FsOut o) {"
                                      "    o.c = float4(1.0, 1.0, 1.0, 1.0)"
                                      "}"
                                      "pipeline White {"
                                      "    Vertex = vertex,"
                                      "    Fragment = fragment,"
                                      "}";

    lesl::CompilationResult cr = lesl::compile(
        white_shader,
        "White",
        lesl::sdl::SDL3BindingManager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform }
    );

    ASSERT_TRUE(cr.is_ok());

    ASSERT_TRUE(spv_validate(cr.compiled_program.data(), cr.compiled_program.size()));
}

TEST(Compilation, Empty) {
    lesl::CompilationResult cr = lesl::compile(
        "",
        "Anything",
        lesl::sdl::SDL3BindingManager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform }
    );

    ASSERT_TRUE(!cr.is_ok());

    ASSERT_TRUE(has_error(cr, lesl::ErrorType::NoPipeline));
}

TEST(Compilation, EmptyFunctions) {
    static const char* mostly_empty = "function vertex () -> () { }"
                                      "function fragment () -> () { }"
                                      "pipeline Nothing {"
                                      "    Vertex = vertex,"
                                      "    Fragment = fragment,"
                                      "}";
    lesl::CompilationResult cr = lesl::compile(
        mostly_empty,
        "Nothing",
        lesl::sdl::SDL3BindingManager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform }
    );

    ASSERT_TRUE(cr.is_ok());

    ASSERT_TRUE(spv_validate(cr.compiled_program.data(), cr.compiled_program.size()));
}
