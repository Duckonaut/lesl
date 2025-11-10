#include "common.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_time.h>
#include <cstddef>

struct Vertex {
    float pos[4];
    float uv[2];
};

static Vertex vertices[] = {
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 3.0f, -1.0f, 0.0f, 1.0f }, { 2.0f, 0.0f } },
    { { -1.0f, 3.0f, 0.0f, 1.0f }, { 0.0f, 2.0f } },
};

struct Uniforms {
    float frame[4];
    float size[2];
    float _padding0[2];
    float time;
};

class Outline : public Example {
  public:
    void init(SDL_Window* window, SDL_GPUDevice* device) override {
        FileData unified_shader = readFile("../shaders/lesl/outline.spv");
        Uint8* image_data = readImageRGBA("../assets/pixels.png", &img_width, &img_height);

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
            .num_uniform_buffers = 0,
            .props = 0,
        };

        SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

        shaderCreateInfo.entrypoint = "fragment";
        shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shaderCreateInfo.num_uniform_buffers = 1;
        shaderCreateInfo.num_samplers = 1;

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
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = offsetof(Vertex, uv),
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
            },
            .target_info = {
                .color_target_descriptions = &color_target_description,
                .num_color_targets = 1,
                .has_depth_stencil_target = false,
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

        SDL_GPUSamplerCreateInfo sampler_desc = {
            .min_filter = SDL_GPU_FILTER_NEAREST,
            .mag_filter = SDL_GPU_FILTER_NEAREST,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .mip_lod_bias = 0.0f,
            .max_anisotropy = 1.0f,
            .min_lod = 0.0f,
            .max_lod = 1000.0f,
            .props = 0,
        };

        sampler = SDL_CreateGPUSampler(device, &sampler_desc);

        SDL_GPUTextureCreateInfo texture_desc;
        texture_desc.width = img_width;
        texture_desc.height = img_height;
        texture_desc.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        texture_desc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        texture_desc.type = SDL_GPU_TEXTURETYPE_2D;
        texture_desc.layer_count_or_depth = 1;
        texture_desc.num_levels = 1;
        texture_desc.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture = SDL_CreateGPUTexture(device, &texture_desc);

        // load phase
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferCreateInfo transfer_create_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeof(vertices),
            .props = 0,
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

        transfer_create_info.size = img_width * img_height * 4;
        transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_create_info);
        mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);

        memcpy(mapped_data, image_data, (size_t)(img_width * img_height * 4));

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

        SDL_GPUTextureRegion texture_region = {
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = img_width,
            .h = img_height,
            .d = 1,
        };

        SDL_GPUTextureTransferInfo texture_source = {
            .transfer_buffer = transfer_buffer,
            .offset = 0,
            .pixels_per_row = img_width,
            .rows_per_layer = img_height,
        };

        SDL_UploadToGPUTexture(copy_pass, &texture_source, &texture_region, false);

        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

        SDL_EndGPUCopyPass(copy_pass);

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    void event(SDL_Event*) override {}

    void update() override {
    }

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

        SDL_GPUTextureSamplerBinding sampler_binding = {
            .texture = texture,
            .sampler = sampler,
        };

        SDL_BindGPUFragmentSamplers(pass, 0, &sampler_binding, 1);

        Uniforms uniforms_data = {
            .frame = { 0.0f, 0.0f, 1.0f, 1.0f },
            .size = { static_cast<float>(img_width), static_cast<float>(img_height) },
            ._padding0 = { 0.0f, 0.0f },
            .time = static_cast<float>(SDL_GetTicks()) / 1000.0f,
        };

        SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms_data, sizeof(Uniforms));


        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

        SDL_EndGPURenderPass(pass);

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    void quit(SDL_GPUDevice* device) override {
        SDL_ReleaseGPUSampler(device, sampler);
        SDL_ReleaseGPUTexture(device, texture);

        SDL_ReleaseGPUBuffer(device, vertex_buffer);
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }

  private:
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUTexture* texture;
    SDL_GPUSampler* sampler;

    uint32_t img_width;
    uint32_t img_height;
};

Example* createExample() {
    return new Outline();
}
