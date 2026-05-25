#include "lesl/sdl.hpp"
#include "gtest/gtest.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL.h>

#include <SDL3/SDL_pixels.h>
#include <cstdlib>
#include <lesl/lesl.hpp>

#include "capture_utils.hpp"

#include <functional>

TEST(Captures, Triangle) {
    struct ScreenVertex {
        float pos[4];
    };

    static ScreenVertex g_screen_vertices[] = {
        { { -1.0f, -1.0f, 0.0f, 1.0f } },
        { { 3.0f, -1.0f, 0.0f, 1.0f } },
        { { -1.0f, 3.0f, 0.0f, 1.0f } },
    };
#if ENABLE_RENDEROC
    rd_try_load();
#endif

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
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform,
        }
    );

    ASSERT_TRUE(cr.is_ok());

    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    ASSERT_TRUE(device);

    SDL_GPUGraphicsPipeline* p = lesl::sdl::create_graphics_pipeline(
        device,
        cr,
        { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB }
    );

    ASSERT_TRUE(p);

    SDL_GPUBuffer* screen_buffer = gpu_buffer_from_slice(
        device,
        g_screen_vertices,
        sizeof(g_screen_vertices),
        SDL_GPU_BUFFERUSAGE_VERTEX,
        "screen"
    );

    SDL_GPUTexture* tex = gpu_texture_from_slice(
        device,
        nullptr,
        0,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        32,
        32
    );

    SDL_GPUTransferBuffer* transfer_buffer = nullptr;

#if ENABLE_RENDEROC
    rd_start();
#endif

    with_cmd(
        device,
        [p,
         tex,
         screen_buffer,
         &transfer_buffer](SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
            SDL_GPUColorTargetInfo ctarget_info;
            ctarget_info.cycle = false;
            ctarget_info.texture = tex;
            ctarget_info.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
            ctarget_info.resolve_texture = nullptr;
            ctarget_info.load_op = SDL_GPU_LOADOP_CLEAR;
            ctarget_info.store_op = SDL_GPU_STOREOP_STORE;
            ctarget_info.mip_level = 0;
            ctarget_info.layer_or_depth_plane = 0;

            auto pass = SDL_BeginGPURenderPass(cmd, &ctarget_info, 1, nullptr);

            SDL_BindGPUGraphicsPipeline(pass, p);

            SDL_GPUBufferBinding binding;
            binding.buffer = screen_buffer;
            binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

            SDL_EndGPURenderPass(pass);

            transfer_buffer = gpu_download_texture(device, cmd, tex, 32, 32, 4);
        }
    );

#if ENABLE_RENDEROC
    rd_end();
#endif

    std::vector<uint8_t> ideal;
    ideal.resize(32 * 32 * 4, 255);

    gpu_compare_transfer_and_release(device, transfer_buffer, ideal.data(), ideal.size());

    SDL_ReleaseGPUBuffer(device, screen_buffer);
    SDL_ReleaseGPUTexture(device, tex);

    SDL_DestroyGPUDevice(device);

#if ENABLE_RENDEROC
    rd_shutdown();
#endif
}

TEST(Captures, Texture) {
    struct Vertex {
        float pos[4];
        float uv[2];
    };

    static Vertex g_screen_vertices[] = {
        { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, -1.0f } },
        { { 3.0f, -1.0f, 0.0f, 1.0f }, { 2.0f, 0.0f } },
        { { -1.0f, 3.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    };
#if ENABLE_RENDEROC
    rd_try_load();
#endif

    static const char* white_shader = "struct VsIn { float4 p, float2 uv }"
                                      "struct Link { float2 uv }"
                                      "struct FsOut { float4 c }"
                                      "function vertex (VsIn v) -> (Link o) {"
                                      "    o.uv = v.uv"
                                      "    POSITION = v.p"
                                      "}"
                                      "function fragment (Link i, sampler2D s) -> (FsOut o) {"
                                      "    o.c = sample2D(s, i.uv)"
                                      "}"
                                      "pipeline Tex {"
                                      "    Vertex = vertex,"
                                      "    Fragment = fragment,"
                                      "}";

    lesl::CompilationResult cr = lesl::compile(
        white_shader,
        "Tex",
        lesl::sdl::SDL3BindingManager{
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform,
        }
    );

    ASSERT_TRUE(cr.is_ok());

    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    ASSERT_TRUE(device);

    SDL_GPUGraphicsPipeline* p = lesl::sdl::create_graphics_pipeline(
        device,
        cr,
        { SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB }
    );

    ASSERT_TRUE(p);

    SDL_GPUBuffer* screen_buffer = gpu_buffer_from_slice(
        device,
        g_screen_vertices,
        sizeof(g_screen_vertices),
        SDL_GPU_BUFFERUSAGE_VERTEX,
        "screen"
    );

    SDL_GPUTexture* tex = gpu_texture_from_slice(
        device,
        nullptr,
        0,
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        32,
        32
    );

    std::vector<uint8_t> noise;
    noise.reserve(32 * 32 * 4);

    srand(0);

    for (int i = 0; i < 32 * 32; i++) {
        noise.push_back((uint8_t)rand());
        noise.push_back((uint8_t)rand());
        noise.push_back((uint8_t)rand());
        noise.push_back(255);
    }

    SDL_GPUTexture* noise_tex = gpu_texture_from_slice(
        device,
        noise.data(),
        noise.size(),
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        32,
        32
    );

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.enable_compare = false;
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;

    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device, &sampler_info);

    SDL_GPUTransferBuffer* transfer_buffer = nullptr;

#if ENABLE_RENDEROC
    rd_start();
#endif

    with_cmd(
        device,
        [p,
         tex,
         screen_buffer,
         noise_tex,
         sampler,
         &transfer_buffer](SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
            SDL_GPUColorTargetInfo ctarget_info;
            ctarget_info.cycle = false;
            ctarget_info.texture = tex;
            ctarget_info.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
            ctarget_info.resolve_texture = nullptr;
            ctarget_info.load_op = SDL_GPU_LOADOP_CLEAR;
            ctarget_info.store_op = SDL_GPU_STOREOP_STORE;
            ctarget_info.mip_level = 0;
            ctarget_info.layer_or_depth_plane = 0;

            auto pass = SDL_BeginGPURenderPass(cmd, &ctarget_info, 1, nullptr);

            SDL_BindGPUGraphicsPipeline(pass, p);

            SDL_GPUBufferBinding binding;
            binding.buffer = screen_buffer;
            binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);


            SDL_GPUTextureSamplerBinding sampler_binding;
            sampler_binding.sampler = sampler;
            sampler_binding.texture = noise_tex;

            SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);

            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

            SDL_EndGPURenderPass(pass);

            transfer_buffer = gpu_download_texture(device, cmd, tex, 32, 32, 4);
        }
    );

#if ENABLE_RENDEROC
    rd_end();
#endif

    gpu_compare_transfer_and_release(device, transfer_buffer, noise.data(), noise.size());

    SDL_ReleaseGPUBuffer(device, screen_buffer);
    SDL_ReleaseGPUTexture(device, tex);
    SDL_ReleaseGPUTexture(device, noise_tex);
    SDL_ReleaseGPUSampler(device, sampler);

    SDL_DestroyGPUDevice(device);

#if ENABLE_RENDEROC
    rd_shutdown();
#endif
}
