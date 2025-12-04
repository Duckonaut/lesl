#include "common.hpp"

#include "log.hpp"

#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cstddef>
#include <vector>

struct Vertex {
    float pos[4];
    float normal[3];
    float uv[2];
};

const static Vertex screen_vertices[] = {
    // positions        // normals         // uvs
    { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 3.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 2.0f, 0.0f } },
    { { -1.0f, 3.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 2.0f } },
};

constexpr SDL_GPUVertexAttribute vertex_attributes[] = {
    {
        .location = 0,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
        .offset = offsetof(Vertex, pos),
    },
    {
        .location = 1,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        .offset = offsetof(Vertex, normal),
    },
    {
        .location = 2,
        .buffer_slot = 0,
        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        .offset = offsetof(Vertex, uv),
    },
};

struct Uniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

// PBR example
class PBR : public Example {
  public:

    SDL_GPUTexture* create_sampler_texture(
        SDL_GPUDevice* device,
        const char* path,
        SDL_GPUTextureFormat format
    ) {
        uint32_t width, height;
        uint8_t* image_data = readImageRGBA(path, &width, &height);
        if (!image_data) {
            Log::error("failed to read image: %s", path);
            return nullptr;
        }

        SDL_GPUTextureCreateInfo texture_create_info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = format,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };

        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texture_create_info);
        if (!texture) {
            Log::error("failed to create texture for image: %s", path);
            freeImageData(image_data);
            return nullptr;
        }


        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferCreateInfo transfer_create_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = width * height * 4,
        };

        SDL_GPUTransferBuffer* transfer_buffer =
            SDL_CreateGPUTransferBuffer(device, &transfer_create_info);

        void* mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);

        memcpy(mapped_data, image_data, width * height * 4);

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

        SDL_GPUTextureTransferInfo source = {
            .transfer_buffer = transfer_buffer,
            .offset = 0,
            .pixels_per_row = width,
            .rows_per_layer = height,
        };

        SDL_GPUTextureRegion destination = {
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = width,
            .h = height,
            .d = 1,
        };

        SDL_UploadToGPUTexture(copy_pass, &source, &destination, true);

        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

        SDL_EndGPUCopyPass(copy_pass);

        SDL_SubmitGPUCommandBuffer(cmd);

        freeImageData(image_data);

        return texture;
    }

    void init(SDL_Window* window, SDL_GPUDevice* device) override {
        FileData pbr_lighting_shader = readFile("../shaders/lesl/pbr_lighting.spv");
        if (!pbr_lighting_shader.data) {
            Log::error("failed to read pbr lighting shader");
            return;
        }

        FileData pbr_gbuffer_shader = readFile("../shaders/lesl/pbr_gbuffer.spv");
        if (!pbr_gbuffer_shader.data) {
            Log::error("failed to read pbr gbuffer shader");
            freeFileData(pbr_lighting_shader);
            return;
        }

        SDL_GPUTextureFormat swapchain_format =
            SDL_GetGPUSwapchainTextureFormat(device, window);

        SDL_GPUShaderCreateInfo shaderCreateInfo = {
            .code_size = pbr_lighting_shader.size,
            .code = (Uint8*)pbr_lighting_shader.data,
            .entrypoint = "vertex",
            .format = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage = SDL_GPU_SHADERSTAGE_VERTEX,
            .num_samplers = 0,
            .num_storage_textures = 0,
            .num_storage_buffers = 0,
            .num_uniform_buffers = 0,
            .props = 0,
        };

        SDL_GPUShader* lighting_vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

        shaderCreateInfo.entrypoint = "fragment";
        shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shaderCreateInfo.num_samplers = 3; // albedo, normal, ao-roughness-metallic

        SDL_GPUShader* lighting_fragment_shader =
            SDL_CreateGPUShader(device, &shaderCreateInfo);

        shaderCreateInfo.code_size = pbr_gbuffer_shader.size;
        shaderCreateInfo.code = (Uint8*)pbr_gbuffer_shader.data;
        shaderCreateInfo.entrypoint = "vertex";
        shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        shaderCreateInfo.num_samplers = 0;
        shaderCreateInfo.num_uniform_buffers = 1; // matrices

        SDL_GPUShader* gbuffer_vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);
        shaderCreateInfo.entrypoint = "fragment";
        shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        shaderCreateInfo.num_uniform_buffers = 0;
        shaderCreateInfo.num_samplers = 3;

        SDL_GPUShader* gbuffer_fragment_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

        freeFileData(pbr_lighting_shader);
        freeFileData(pbr_gbuffer_shader);

        SDL_GPUColorTargetDescription gbuffer_target_descriptions [] = {
            // albedo
            {
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
                .blend_state = {
                    .enable_blend = false,
                },
            },
            // normal
            {
                .format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                .blend_state = {
                    .enable_blend = false,
                },
            },
            // ao-roughness-metallic
            {
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                .blend_state = {
                    .enable_blend = false,
                },
            },
        };

        SDL_GPUColorTargetDescription color_target_description = {
            .format = swapchain_format,
            .blend_state = {
                .enable_blend = false,
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

        SDL_GPUGraphicsPipelineCreateInfo lighting_pipeline_create_info = {
            .vertex_shader = lighting_vertex_shader,
            .fragment_shader = lighting_fragment_shader,
            .vertex_input_state = {
                .vertex_buffer_descriptions = vertex_buffer_descriptions,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertex_attributes,
                .num_vertex_attributes = 3,
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = {
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            },
            .target_info = {
                .color_target_descriptions = &color_target_description,
                .num_color_targets = 1,
            },
            .props = 0,
        };

        lighting_pipeline =
            SDL_CreateGPUGraphicsPipeline(device, &lighting_pipeline_create_info);

        SDL_GPUGraphicsPipelineCreateInfo gbuffer_pipeline_create_info = {
            .vertex_shader = gbuffer_vertex_shader,
            .fragment_shader = gbuffer_fragment_shader,
            .vertex_input_state = {
                .vertex_buffer_descriptions = vertex_buffer_descriptions,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertex_attributes,
                .num_vertex_attributes = 3,
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = {
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            },
            .target_info = {
                .color_target_descriptions = gbuffer_target_descriptions,
                .num_color_targets = 3,
            },
            .props = 0,
        };

        gbuffer_pipeline = SDL_CreateGPUGraphicsPipeline(device, &gbuffer_pipeline_create_info);

        SDL_ReleaseGPUShader(device, lighting_vertex_shader);
        SDL_ReleaseGPUShader(device, lighting_fragment_shader);
        SDL_ReleaseGPUShader(device, gbuffer_vertex_shader);
        SDL_ReleaseGPUShader(device, gbuffer_fragment_shader);

        SDL_GPUBufferCreateInfo screen_vertex_buffer_create_info = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = sizeof(screen_vertices),
            .props = 0,
        };

        screen_vertex_buffer = SDL_CreateGPUBuffer(device, &screen_vertex_buffer_create_info);

        std::vector<ModelVertex> model_vertices;
        std::vector<uint32_t> model_indices;

        if (!loadModel("../assets/pbr/sphere.glb", model_vertices, model_indices)) {
            Log::error("failed to load model");
            std::abort();
            return;
        }

        std::vector<Vertex> vertices(model_vertices.size());
        for (size_t i = 0; i < model_vertices.size(); ++i) {
            vertices[i].pos[0] = model_vertices[i].position.x;
            vertices[i].pos[1] = model_vertices[i].position.y;
            vertices[i].pos[2] = model_vertices[i].position.z;
            vertices[i].pos[3] = 1.0f;
            vertices[i].normal[0] = model_vertices[i].normal.x;
            vertices[i].normal[1] = model_vertices[i].normal.y;
            vertices[i].normal[2] = model_vertices[i].normal.z;
            vertices[i].uv[0] = model_vertices[i].uv.x;
            vertices[i].uv[1] = model_vertices[i].uv.y;
        }

        model_index_count = (uint32_t)model_indices.size();

        SDL_GPUBufferCreateInfo vertex_buffer_create_info = {
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = (uint32_t)(sizeof(Vertex) * vertices.size()),
            .props = 0,
        };

        model_vertex_buffer = SDL_CreateGPUBuffer(device, &vertex_buffer_create_info);

        SDL_GPUBufferCreateInfo index_buffer_create_info = {
            .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size = (uint32_t)(sizeof(uint32_t) * model_indices.size()),
            .props = 0,
        };

        model_index_buffer = SDL_CreateGPUBuffer(device, &index_buffer_create_info);

        uint32_t window_width = 1024;
        uint32_t window_height = 1024;

        SDL_GPUTextureCreateInfo gbuffer_texture_create_info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = window_width,
            .height = window_height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };

        gbuffer_albedo = SDL_CreateGPUTexture(device, &gbuffer_texture_create_info);

        gbuffer_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        gbuffer_normal = SDL_CreateGPUTexture(device, &gbuffer_texture_create_info);

        gbuffer_texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        gbuffer_ao_roughness_metallic = SDL_CreateGPUTexture(device, &gbuffer_texture_create_info);

        albedo_texture =
            create_sampler_texture(device, "../assets/pbr/textures/damaged_plaster_diff_2k.png",
                                   SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
        normal_texture =
            create_sampler_texture(device, "../assets/pbr/textures/damaged_plaster_nor_gl_2k.png",
                                   SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        ao_roughness_metallic_texture =
            create_sampler_texture(device, "../assets/pbr/textures/damaged_plaster_arm_2k.png",
                                   SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);

        SDL_GPUSamplerCreateInfo sampler_create_info = {
            .min_filter = SDL_GPU_FILTER_LINEAR,
            .mag_filter = SDL_GPU_FILTER_LINEAR,
            .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .mip_lod_bias = 0.0f,
            .max_anisotropy = 1.0f,
            .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
            .min_lod = 0.0f,
            .max_lod = 1.0f,
            .enable_anisotropy = false,
            .enable_compare = false,
            .props = 0,
        };

        default_sampler = SDL_CreateGPUSampler(device, &sampler_create_info);

        // load phase
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTransferBufferCreateInfo transfer_create_info = {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = std::max(
                vertex_buffer_create_info.size,
                std::max(index_buffer_create_info.size, screen_vertex_buffer_create_info.size)
            ),
        };

        SDL_GPUTransferBuffer* transfer_buffer =
            SDL_CreateGPUTransferBuffer(device, &transfer_create_info);

        void* mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);

        memcpy(mapped_data, screen_vertices, sizeof(screen_vertices));

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        SDL_GPUTransferBufferLocation source = {
            .transfer_buffer = transfer_buffer,
            .offset = 0,
        };
        SDL_GPUBufferRegion destination = {
            .buffer = screen_vertex_buffer,
            .offset = 0,
            .size = sizeof(screen_vertices),
        };
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);

        memcpy(mapped_data, vertices.data(), sizeof(Vertex) * vertices.size());

        source.transfer_buffer = transfer_buffer;
        destination.buffer = model_vertex_buffer;
        destination.offset = 0;
        destination.size = sizeof(Vertex) * vertices.size();
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);

        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
        mapped_data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);

        memcpy(mapped_data, model_indices.data(), sizeof(uint32_t) * model_indices.size());

        source.transfer_buffer = transfer_buffer;
        destination.buffer = model_index_buffer;
        destination.offset = 0;
        destination.size = sizeof(uint32_t) * model_indices.size();
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);

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

        // gbuffer pass
        
        SDL_GPUColorTargetInfo gbuffer_targets[] = {
            {
                .texture = gbuffer_albedo,
                .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
                .cycle = true,
            },
            {
                .texture = gbuffer_normal,
                .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
                .cycle = true,
            },
            {
                .texture = gbuffer_ao_roughness_metallic,
                .clear_color = { 1.0f, 1.0f, 1.0f, 1.0f },
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE,
                .cycle = true,
            },
        };

        SDL_GPURenderPass* gbuffer_pass =
            SDL_BeginGPURenderPass(cmd, gbuffer_targets, 3, nullptr);

        SDL_BindGPUGraphicsPipeline(gbuffer_pass, gbuffer_pipeline);

        SDL_GPUBufferBinding binding = {
            .buffer = model_vertex_buffer,
            .offset = 0,
        };

        SDL_BindGPUVertexBuffers(gbuffer_pass, 0, &binding, 1);

        binding.buffer = model_index_buffer;
        SDL_BindGPUIndexBuffer(gbuffer_pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(w) / static_cast<float>(h),
            0.1f,
            100.0f
        );

        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4.0f));

        glm::mat4 model = glm::rotate(
            glm::mat4(1.0f),
            static_cast<float>(SDL_GetTicks()) / 1000.0f,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        model = glm::rotate(
            model,
            static_cast<float>(SDL_GetTicks()) / 2000.0f,
            glm::vec3(1.0f, 0.0f, 0.0f)
        );

        Uniforms uniforms = {
            .model = model,
            .view = view,
            .projection = projection,
        };

        SDL_GPUTextureSamplerBinding gbuffer_textures[] = {
            {
                .texture = albedo_texture,
                .sampler = default_sampler,
            },
            {
                .texture = normal_texture,
                .sampler = default_sampler,
            },
            {
                .texture = ao_roughness_metallic_texture,
                .sampler = default_sampler,
            },
        };

        SDL_BindGPUFragmentSamplers(gbuffer_pass, 0, gbuffer_textures, 3);

        SDL_PushGPUVertexUniformData(cmd, 0, &uniforms, sizeof(uniforms));

        SDL_DrawGPUIndexedPrimitives(
            gbuffer_pass,
            model_index_count,
            1,
            0,
            0,
            0
        );

        SDL_EndGPURenderPass(gbuffer_pass);

        SDL_GPUColorTargetInfo target = {
            .texture = swapchain,
            .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .cycle = true,
        };

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);

        SDL_BindGPUGraphicsPipeline(pass, lighting_pipeline);

        SDL_GPUBufferBinding screen_binding = {
            .buffer = screen_vertex_buffer,
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(pass, 0, &screen_binding, 1);

        SDL_GPUTextureSamplerBinding gbuffer_result_textures[] = {
            {
                .texture = gbuffer_albedo,
                .sampler = default_sampler,
            },
            {
                .texture = gbuffer_normal,
                .sampler = default_sampler,
            },
            {
                .texture = gbuffer_ao_roughness_metallic,
                .sampler = default_sampler,
            },
        };

        SDL_BindGPUFragmentSamplers(pass, 0, gbuffer_result_textures, 3);

        SDL_DrawGPUPrimitives(
            pass,
            3,
            1,
            0,
            0
        );

        SDL_EndGPURenderPass(pass);

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    void quit(SDL_GPUDevice* device) override {
        SDL_ReleaseGPUGraphicsPipeline(device, lighting_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(device, gbuffer_pipeline);
        SDL_ReleaseGPUBuffer(device, model_vertex_buffer);
        SDL_ReleaseGPUBuffer(device, model_index_buffer);
        SDL_ReleaseGPUBuffer(device, screen_vertex_buffer);
        SDL_ReleaseGPUTexture(device, gbuffer_albedo);
        SDL_ReleaseGPUTexture(device, gbuffer_normal);
        SDL_ReleaseGPUTexture(device, gbuffer_ao_roughness_metallic);

        SDL_ReleaseGPUTexture(device, albedo_texture);
        SDL_ReleaseGPUTexture(device, normal_texture);
        SDL_ReleaseGPUTexture(device, ao_roughness_metallic_texture);

        SDL_ReleaseGPUSampler(device, default_sampler);
    }

  private:
    SDL_GPUGraphicsPipeline* lighting_pipeline;
    SDL_GPUGraphicsPipeline* gbuffer_pipeline;
    SDL_GPUBuffer* model_vertex_buffer;
    SDL_GPUBuffer* model_index_buffer;

    SDL_GPUSampler* default_sampler;

    uint32_t model_index_count;

    SDL_GPUBuffer* screen_vertex_buffer;

    SDL_GPUTexture* albedo_texture;
    SDL_GPUTexture* normal_texture;
    SDL_GPUTexture* ao_roughness_metallic_texture;

    SDL_GPUTexture* gbuffer_albedo;
    SDL_GPUTexture* gbuffer_normal;
    SDL_GPUTexture* gbuffer_ao_roughness_metallic;
};

Example* createExample() {
    return new PBR();
}
