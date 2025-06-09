#include "common.hpp"

#include "log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstddef>

struct Vertex {
    float pos[4];
    float color[4];
};

static Vertex vertices[] = {
    { { 0.0f, 0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
};

struct Uniforms {
    glm::mat3x4 weird_matrix_a;
    glm::mat4x4 weird_matrix_b;
};

class Matrices : public Example {
  public:
    void init(SDL_Window* window, SDL_GPUDevice* device) override {
        FileData unified_shader = readFile("../shaders/lesl/matrices.spv");

        SDL_GPUTextureFormat swapchain_format =
            SDL_GetGPUSwapchainTextureFormat(device, window);

        SDL_GPUShaderCreateInfo shaderCreateInfo = {
            .code_size = unified_shader.size,
            .code = (Uint8*)unified_shader.data,
            .entrypoint = "vertex",
            .format = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers = 0,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 1,
            .props = 0,
        };

        SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

        shaderCreateInfo.entrypoint = "fragment";
        shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;

        SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

        SDL_GPUColorTargetDescription color_target_description = {
            .format = swapchain_format,
            .blend_state = {
                .enable_blend = false,
            },
        };

        SDL_GPUVertexAttribute vertex_attributes[] = {
            {
                .location = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
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
            .rasterizer_state = {
                .cull_mode = SDL_GPU_CULLMODE_NONE,
                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            },
            .target_info = {
                .color_target_descriptions = &color_target_description,
                .num_color_targets = 1,
            },
            .props = 0,
        };

        pipeline = SDL_CreateGPUGraphicsPipeline(device, &createInfo);

        SDL_ReleaseGPUShader(device, vertex_shader);
        SDL_ReleaseGPUShader(device, fragment_shader);

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

        Uniforms uniforms = {
            .weird_matrix_a = {
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
            },
            .weird_matrix_b = { // this will be a 4x3 matrix, but due to uniform layout rules, it will be padded to 4x4
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f },
                { 0.0f, 0.0f, 0.0f, 0.0f },    
            },
        };

        SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_DrawGPUPrimitives(pass, 36, 1, 0, 0);

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
    SDL_GPUBuffer* uniform_buffer;
};

Example* createExample() {
    return new Matrices();
}
