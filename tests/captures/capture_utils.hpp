#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL.h>

#include <SDL3/SDL_pixels.h>
#include <lesl/lesl.hpp>

#define ENABLE_RENDEROC 1

#if ENABLE_RENDEROC
#include <renderdoc_app.h>

#if WIN32
#include <windows.h>
inline HMODULE g_rd_module;
#else
#include <dlfcn.h>
inline void* g_rd_module = NULL;
#endif

inline RENDERDOC_API_1_6_0* g_renderdoc_api = NULL;

inline void rd_try_load() {
#if WIN32
    g_rd_module = LoadLibrary("C:/Program Files/RenderDoc/renderdoc.dll");
    if (!g_rd_module) {
        return;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        (pRENDERDOC_GetAPI)GetProcAddress(g_rd_module, "RENDERDOC_GetAPI");
#else
    g_rd_module = dlopen("/usr/lib/librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!g_rd_module) {
        return;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        (pRENDERDOC_GetAPI)dlsym(g_rd_module, "RENDERDOC_GetAPI");
#endif

    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&g_renderdoc_api);
    if (ret != 1) {
        g_renderdoc_api = NULL;
    }
}

inline void rd_shutdown() {
    if (g_renderdoc_api && g_rd_module) {
#if WIN32
        FreeLibrary(g_rd_module);
#else
        dlclose(g_rd_module);
#endif
    }
}

inline void rd_start() {
    if (g_renderdoc_api)
        g_renderdoc_api->StartFrameCapture(NULL, NULL);
}

inline void rd_end() {
    if (!g_renderdoc_api)
        return;

    int capture_result = g_renderdoc_api->EndFrameCapture(NULL, NULL);

    uint32_t captureCount = g_renderdoc_api->GetNumCaptures();
    if (capture_result == 1 && captureCount > 0) {
        uint32_t lastCaptureID = captureCount - 1;
        bool inRange = g_renderdoc_api->GetCapture(lastCaptureID, NULL, NULL, NULL);
        if (inRange) {
            if (g_renderdoc_api->IsTargetControlConnected()) {
                g_renderdoc_api->ShowReplayUI();
            } else {
                uint32_t processID = g_renderdoc_api->LaunchReplayUI(1, "");
                assert(processID != 0);
            }
        }
    }
}
#endif

inline void
with_cmd(SDL_GPUDevice* device, std::function<void(SDL_GPUDevice*, SDL_GPUCommandBuffer*)> f) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

    f(device, cmd);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
}

inline SDL_GPUBuffer* gpu_buffer_from_slice(
    SDL_GPUDevice* device,
    void* src,
    size_t len,
    SDL_GPUBufferUsageFlags flags,
    const char* name
) {
    SDL_PropertiesID properties = 0;
    if (name != NULL) {
        properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, name);
    }
    SDL_GPUBufferCreateInfo info = {
        .usage = flags,
        .size = (uint32_t)len,
        .props = properties,
    };
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &info);
    assert(buffer);

    with_cmd(device, [src, len, buffer](SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        assert(copy_pass);

        SDL_GPUTransferBufferCreateInfo transfer_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (uint32_t)len,
            .props = 0,
        };

        SDL_GPUTransferBuffer* transfer_buffer =
            SDL_CreateGPUTransferBuffer(device, &transfer_info);
        assert(transfer_buffer);

        void* data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
        assert(data);

        memcpy(data, src, len);

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

        SDL_GPUTransferBufferLocation location = {
            .transfer_buffer = transfer_buffer,
            .offset = 0,
        };

        SDL_GPUBufferRegion region = { .buffer = buffer, .offset = 0, .size = (uint32_t)len };

        SDL_UploadToGPUBuffer(copy_pass, &location, &region, false);

        SDL_EndGPUCopyPass(copy_pass);
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    });

    if (properties != 0) {
        SDL_DestroyProperties(properties);
    }
    return buffer;
}

inline SDL_GPUTexture* gpu_texture_from_slice(
    SDL_GPUDevice* device,
    void* src,
    size_t len,
    SDL_GPUTextureType type,
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    uint32_t w,
    uint32_t h
) {
    SDL_GPUTextureCreateInfo info = {
        .type = type,
        .format = format,
        .usage = usage,
        .width = w,
        .height = h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &info);
    assert(tex);

    if (src != nullptr) {
        with_cmd(device, [&](SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
            assert(copy_pass);

            SDL_GPUTransferBufferCreateInfo transfer_info = {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = (uint32_t)len,
                .props = 0,
            };

            SDL_GPUTransferBuffer* transfer_buffer =
                SDL_CreateGPUTransferBuffer(device, &transfer_info);
            assert(transfer_buffer);

            void* data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
            assert(data);

            memcpy(data, src, len);

            SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

            SDL_GPUTextureTransferInfo location = {
                .transfer_buffer = transfer_buffer,
                .offset = 0,
                .pixels_per_row = w,
                .rows_per_layer = h,
            };

            SDL_GPUTextureRegion region = {
                .texture = tex,
                .mip_level = 0,
                .layer = 0,
                .x = 0,
                .y = 0,
                .z = 0,
                .w = w,
                .h = h,
                .d = 1,
            };

            SDL_UploadToGPUTexture(copy_pass, &location, &region, false);

            SDL_EndGPUCopyPass(copy_pass);

            SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
        });
    }
    return tex;
}

inline SDL_GPUTransferBuffer* gpu_download_texture(
    SDL_GPUDevice* device,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTexture* tex,
    uint32_t w,
    uint32_t h,
    uint32_t c
) {
    auto cpass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = (w * h * c),
        .props = 0,
    };

    SDL_GPUTransferBuffer* tbuffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);

    SDL_GPUTextureRegion texture_region = {
        .texture = tex,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = 32,
        .h = 32,
        .d = 1,
    };

    SDL_GPUTextureTransferInfo transfer_target = {
        .transfer_buffer = tbuffer,
        .offset = 0,
        .pixels_per_row = 32,
        .rows_per_layer = 32,
    };

    SDL_DownloadFromGPUTexture(cpass, &texture_region, &transfer_target);

    SDL_EndGPUCopyPass(cpass);

    return tbuffer;
}

inline bool gpu_compare_transfer_and_release(
    SDL_GPUDevice* device,
    SDL_GPUTransferBuffer* transfer_buffer,
    void* compared,
    size_t len
) {
    void* data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    assert(data);

    bool r = memcmp(data, compared, len) == 0;

    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    return r;
}
