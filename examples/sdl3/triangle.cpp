#include "common.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstddef>

#include <lesl/lesl.hpp>
#include <lesl/sdl.hpp>

struct Vertex {
    float pos[4];
    float color[4];
};

static Vertex vertices[] = {
    { { 0.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
};

class Triangle : public Example {
  public:
    void init(SDL_Window* window, SDL_GPUDevice* device) override {
        FileData unified_shader = readFile("../shaders/lesl/triangle.lesl");

        SDL_GPUTextureFormat swapchain_format =
            SDL_GetGPUSwapchainTextureFormat(device, window);

        for (int i = 0; i < 200; i++) {
            auto binding_manager = lesl::sdl::SDL3BindingManager(
                lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
            );

            auto cr_ = lesl::compile(
                (const char*)unified_shader.data,
                "Triangle",
                std::move(binding_manager)
            );
        }
        auto binding_manager = lesl::sdl::SDL3BindingManager(
            lesl::sdl::SDL3BindingManager::BindingAllocationMode::SingleInputMultipleUniform
        );

        auto cr = lesl::compile(
            (const char*)unified_shader.data,
            "Triangle",
            std::move(binding_manager)
        );

        if (!cr.is_ok()) {
            assert(false && "Pipeline compilation failed");
        }

        pipeline = lesl::sdl::create_graphics_pipeline(device, cr, { swapchain_format });

        if (!pipeline) {
            assert(false && "Pipeline creation failed");
        }

        freeFileData(unified_shader);

        SDL_GPUBufferCreateInfo buffer_create_info = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = sizeof(vertices),
            .props = 0,
        };

        vertex_buffer = SDL_CreateGPUBuffer(device, &buffer_create_info);

        // load phase
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferCreateInfo transfer_create_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(vertices),
        };

        SDL_GPUTransferBuffer* transfer_buffer =
            SDL_CreateGPUTransferBuffer(device, &transfer_create_info);

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
    }

    void event(SDL_Event* event) override {}

    void update() override {}

    void render(SDL_Window* window, SDL_GPUDevice* device) override {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUTexture* swapchain;
        Uint32 w, h;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            return;
        }

        SDL_GPUColorTargetInfo target = {
            .texture = swapchain,
            .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .cycle = false,
        };

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);

        SDL_BindGPUGraphicsPipeline(pass, pipeline);

        SDL_GPUBufferBinding binding = {
            .buffer = vertex_buffer,
            .offset = 0,
        };

        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

        SDL_EndGPURenderPass(pass);

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    void quit(SDL_GPUDevice* device) override {
        SDL_ReleaseGPUBuffer(device, vertex_buffer);
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

  private:
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUBuffer* vertex_buffer;
};

Example* createExample() {
    return new Triangle();
}
