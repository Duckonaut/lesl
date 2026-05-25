#include "lesl/sdl.hpp"
#include "gtest/gtest.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL.h>

#include <lesl/lesl.hpp>

TEST(Captures, Triangle) {
    static const char* white_shader = "struct VsIn {"
                                      "    float4 p,"
                                      "}"
                                      "function vertex (VsIn v) -> () {"
                                      "    POSITION = v.p"
                                      "}"
                                      "function fragment () -> (FsOut o) {"
                                      "    o.c = float4(1.0, 1.0, 1.0, 1.0"
                                      "}"
                                      "pipeline White {"
                                      "    Vertex = vertex,"
                                      "    Fragment = fragment,"
                                      "}";

    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    ASSERT_TRUE(device);

    lesl::CompilationResult cr = lesl::compile(
        white_shader,
        "White",
        lesl::sdl::SDL3BindingManager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform,
        }
    );

    ASSERT_TRUE(cr.is_ok());

    SDL_GPUGraphicsPipeline* p = lesl::sdl::create_graphics_pipeline(
        device,
        cr,
        { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB }
    );

    ASSERT_TRUE(p);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);

    SDL_DestroyGPUDevice(device);
    EXPECT_EQ(1, 1);
}
