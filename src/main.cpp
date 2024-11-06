#include "SDL3/SDL_stdinc.h"
#include "log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>
#include <cstddef>

#ifdef RELEASE
#define WINDOW_TITLE PROJECT_NAME
#define GAME_VERSION "v" PROJECT_VERSION
#else
#define WINDOW_TITLE "dev: " PROJECT_NAME
#define GAME_VERSION "v" PROJECT_VERSION " (dev " PROJECT_COMMIT_HASH ")"
#endif

SDL_AppResult SDL_Fail(void) {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]);
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event);
SDL_AppResult SDL_AppIterate(void* appstate);
void SDL_AppQuit(void* appstate, SDL_AppResult result);

struct Vertex {
    float pos[2];
    float color[4];
};

static Vertex vertices[] = {
    { { 0.0f, 0.5f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
};

struct App {
    SDL_Window* window;
    SDL_GPUDevice* device;

    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUBuffer* vertex_buffer;

    App(SDL_Window* window,
        SDL_GPUDevice* device,
        SDL_GPUGraphicsPipeline* pipeline,
        SDL_GPUBuffer* vertex_buffer)
        : window(window), device(device), pipeline(pipeline), vertex_buffer(vertex_buffer) {}
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    Log::init();

    Log::info(PROJECT_NAME " " GAME_VERSION);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return SDL_Fail();
    }

    SDL_Window* window = SDL_CreateWindow(WINDOW_TITLE, 720, 540, SDL_WINDOW_RESIZABLE);
    if (!window) {
        return SDL_Fail();
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!device) {
        return SDL_Fail();
    }

    Log::info("GPU device created");

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        Log::error("failed to claim window for GPU device");
        return SDL_Fail();
    }

    Log::info("window claimed for GPU device");

    size_t vertex_shader_code_size = 0;
    const Uint8* vertex_shader_code =
        (const Uint8*)SDL_LoadFile("shaders/simple.vert.spv", &vertex_shader_code_size);

    size_t fragment_shader_code_size = 0;
    const Uint8* fragment_shader_code =
        (const Uint8*)SDL_LoadFile("x.spv", &fragment_shader_code_size);

    SDL_GPUShaderCreateInfo shaderCreateInfo = {
        .code_size = vertex_shader_code_size,
        .code = vertex_shader_code,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = 0,
        .props = 0,
    };

    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

    shaderCreateInfo.code_size = fragment_shader_code_size;
    shaderCreateInfo.code = fragment_shader_code;
    shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

    SDL_GPUColorTargetDescription
        color_target_description = { .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                     .blend_state = {
                                         .enable_blend = false,
                                     } };

    SDL_GPUVertexAttribute vertex_attributes[] = {
        {
            .location = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = offsetof(Vertex, pos),
        },
        {
            .location = 1,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = offsetof(Vertex, color),
        },
    };

    SDL_GPUVertexBufferDescription vertex_buffer_descriptions[] = {
        {
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo createInfo = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertex_buffer_descriptions,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertex_attributes,
            .num_vertex_attributes = 2,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = {
            .color_target_descriptions = &color_target_description,
            .num_color_targets = 1,
            .has_depth_stencil_target = false,
        },
        .props = 0,
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &createInfo);

    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    SDL_free((void*)vertex_shader_code);
    SDL_free((void*)fragment_shader_code);

    SDL_GPUBufferCreateInfo buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(vertices),
        .props = 0,
    };

    SDL_GPUBuffer* vertex_buffer = SDL_CreateGPUBuffer(device, &buffer_create_info);

    // load phase
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferCreateInfo transfer_create_info= {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(vertices),
    };

    SDL_GPUTransferBuffer* transfer_buffer =  SDL_CreateGPUTransferBuffer(device, &transfer_create_info);

    void* mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);

    memcpy(mapped_data, vertices, sizeof(vertices));

    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    SDL_GPUTransferBufferLocation source = {
        .transfer_buffer = transfer_buffer,
        .offset = 0,
    };

    SDL_GPUBufferRegion destination = {
        .buffer = vertex_buffer,
        .offset = 0,
        .size = sizeof(vertices),
    };

    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    SDL_EndGPUCopyPass(copy_pass);

    SDL_SubmitGPUCommandBuffer(cmd);

    App* app = new App(window, device, pipeline, vertex_buffer);

    *appstate = app;

    return SDL_APP_CONTINUE;
}

void app_event_resize(SDL_Event* event) {
    SDL_WindowEvent* window_event = (SDL_WindowEvent*)event;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        app_event_resize(event);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;

    SDL_GPUDevice* device = app->device;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

    SDL_GPUTexture* swapchain;
    Uint32 w, h;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, app->window, &swapchain, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return SDL_APP_CONTINUE;
    }

    SDL_GPUColorTargetInfo target = {
        .texture = swapchain,
        .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = false,
    };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);

    SDL_BindGPUGraphicsPipeline(pass, app->pipeline);

    SDL_GPUBufferBinding binding = {
        .buffer = app->vertex_buffer,
        .offset = 0,
    };

    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    App* app = (App*)appstate;

    SDL_ReleaseGPUBuffer(app->device, app->vertex_buffer);
    SDL_ReleaseGPUGraphicsPipeline(app->device, app->pipeline);

    SDL_DestroyGPUDevice(app->device);
    SDL_DestroyWindow(app->window);

    delete app;

    Log::info("everything terminated successfully");
    Log::shutdown();
}
