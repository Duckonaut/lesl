#include <SDL3/SDL.h>

#include <lesl/lesl.hpp>
#include <lesl/integration.hpp>

#include <stdint.h>
#include <unordered_map>

namespace lesl::sdl {

SDL_GPUVertexElementFormat format_from_type(Ref<TypeInfo> t) {
    static std::unordered_map<std::string, SDL_GPUVertexElementFormat> formats = {
        { "float", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT },
        { "float2", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2 },
        { "float3", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3 },
        { "float4", SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4 },
        { "int", SDL_GPU_VERTEXELEMENTFORMAT_INT },
        { "int2", SDL_GPU_VERTEXELEMENTFORMAT_INT2 },
        { "int3", SDL_GPU_VERTEXELEMENTFORMAT_INT3 },
        { "int4", SDL_GPU_VERTEXELEMENTFORMAT_INT4 },
        { "uint", SDL_GPU_VERTEXELEMENTFORMAT_UINT },
        { "uint2", SDL_GPU_VERTEXELEMENTFORMAT_UINT2 },
        { "uint3", SDL_GPU_VERTEXELEMENTFORMAT_UINT3 },
        { "uint4", SDL_GPU_VERTEXELEMENTFORMAT_UINT4 },
    };

    return formats[t->name.to_string()];
}

SDL_GPUPrimitiveType parse_primitive(std::string s) {
    static std::unordered_map<std::string, SDL_GPUPrimitiveType> prims = {
        { "TriangleList", SDL_GPU_PRIMITIVETYPE_TRIANGLELIST },
        { "TriangleStrip", SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP },
        { "LineList", SDL_GPU_PRIMITIVETYPE_LINELIST },
        { "LineStrip", SDL_GPU_PRIMITIVETYPE_LINESTRIP },
        { "PointList", SDL_GPU_PRIMITIVETYPE_POINTLIST },
    };

    return prims[s];
}

SDL_GPUCullMode parse_cull_mode(std::string s) {
    static std::unordered_map<std::string, SDL_GPUCullMode> cull_modes = {
        { "None", SDL_GPU_CULLMODE_NONE },
        { "Off", SDL_GPU_CULLMODE_NONE },
        { "Front", SDL_GPU_CULLMODE_FRONT },
        { "Back", SDL_GPU_CULLMODE_BACK },
    };

    return cull_modes[s];
}

SDL_GPUFillMode parse_fill_mode(std::string s) {
    static std::unordered_map<std::string, SDL_GPUFillMode> fill_mode = {
        { "Fill", SDL_GPU_FILLMODE_FILL },
        { "Line", SDL_GPU_FILLMODE_LINE },
    };

    return fill_mode[s];
}

SDL_GPUFrontFace parse_front_face(std::string s) {
    static std::unordered_map<std::string, SDL_GPUFrontFace> front_faces = {
        { "Clockwise", SDL_GPU_FRONTFACE_CLOCKWISE },
        { "CW", SDL_GPU_FRONTFACE_CLOCKWISE },
        { "CounterClockwise", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
        { "CCW", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
    };

    return front_faces[s];
}

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    CompilationResult cr,
    std::vector<SDL_GPUTextureFormat> color_target_formats,
    std::vector<SDL_GPUVertexAttribute>* vertex_attributes = nullptr,
    std::vector<SDL_GPUVertexBufferDescription>* vertex_buffer_descriptions = nullptr
) {
    if (!cr.is_ok()) {
        return nullptr;
    }

    if (vertex_attributes && !vertex_buffer_descriptions) {
        return nullptr;
    }

    std::vector<char> unified_shader = cr.compiled_program;

    // TO BE DETERMINED USING PPARAMS AND COMPILATION INSIGHT
    const char* entry_point_vertex = cr.pipeline_parameters["Vertex"].c_str();
    const char* entry_point_fragment = cr.pipeline_parameters["Fragment"].c_str();

    SDL_GPUShaderCreateInfo shaderCreateInfo = {
        .code_size = unified_shader.size(),
        .code = (Uint8*)unified_shader.data(),
        .entrypoint = entry_point_vertex,
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = cr.vertex_binds.num_samplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = cr.vertex_binds.num_uniform_buffers,
        .props = 0,
    };

    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

    shaderCreateInfo.entrypoint = entry_point_fragment;
    shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shaderCreateInfo.num_samplers = cr.fragment_binds.num_samplers;
    shaderCreateInfo.num_uniform_buffers = cr.fragment_binds.num_uniform_buffers;

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(device, &shaderCreateInfo);

    std::vector<SDL_GPUColorTargetDescription> color_target_descriptions;

    for (auto& ctf : color_target_formats) {
        color_target_descriptions.push_back(
            {
                .format = ctf,
                .blend_state = {
                    .enable_blend = false,
                },
            }
        );
    }

    std::vector<SDL_GPUVertexAttribute> vas;
    std::vector<SDL_GPUVertexBufferDescription> vbds;

    if (vertex_attributes) {
        vas = *vertex_attributes;
    } else {
        uint32_t offset = 0;
        uint32_t last_size = 0;
        uint32_t last_set = 0;
        uint32_t max_alignment = 0;

        for (auto& b : cr.vertex_binds.binds) {
            if (b.type == BindType::Input) {
                if (last_size != 0) {
                    offset += std::max(last_size, b.alignment);
                }
                max_alignment = std::max(max_alignment, b.alignment);
                if (b.set != last_set) {
                    vbds.push_back(
                        {
                            .slot = static_cast<Uint32>(vbds.size()),
                            .pitch = offset,
                            .input_rate = vbds.size() == 0 ? SDL_GPU_VERTEXINPUTRATE_VERTEX
                                                           : SDL_GPU_VERTEXINPUTRATE_INSTANCE,
                            .instance_step_rate = 0,
                        }
                    );
                    offset = 0;
                }
                vas.push_back(
                    {
                        .location = b.slot,
                        .buffer_slot = b.set,
                        .format = format_from_type(b.binding_type),
                        .offset = offset,
                    }
                );
                last_size = b.size;
                last_set = b.set;
            }
        }
        uint32_t pitch = ((offset + max_alignment) / max_alignment) * max_alignment;
        vbds.push_back(
            {
                .slot = static_cast<Uint32>(vbds.size()),
                .pitch = pitch,
                .input_rate = vbds.size() == 0 ? SDL_GPU_VERTEXINPUTRATE_VERTEX
                                               : SDL_GPU_VERTEXINPUTRATE_INSTANCE,
                .instance_step_rate = 0,
            }
        );
    }

    if (vertex_buffer_descriptions) {
        vbds = *vertex_buffer_descriptions;
    }
    SDL_GPUPrimitiveType prim =
        parse_primitive(cr.pipeline_parameters[CONVENTION_PRIMITIVE_TYPE_KEY]);

    SDL_GPURasterizerState raster{};
    raster.cull_mode = parse_cull_mode(cr.pipeline_parameters[CONVENTION_CULL_MODE_KEY]);
    raster.fill_mode = parse_fill_mode(cr.pipeline_parameters[CONVENTION_FILL_MODE_KEY]);
    raster.front_face = parse_front_face(cr.pipeline_parameters[CONVENTION_FRONT_FACE_KEY]);

    SDL_GPUGraphicsPipelineCreateInfo createInfo = {
            .vertex_shader = vertex_shader,
            .fragment_shader = fragment_shader,
            .vertex_input_state = {
                .vertex_buffer_descriptions = vbds.data(),
                .num_vertex_buffers = static_cast<Uint32>(vbds.size()),
                .vertex_attributes = vas.data(),
                .num_vertex_attributes = static_cast<Uint32>(vas.size()),
            },
            .primitive_type = prim,
            .rasterizer_state = raster,
            .multisample_state = {
                .sample_count = SDL_GPU_SAMPLECOUNT_1,
                .sample_mask = 0,
                .enable_mask = false,
                .enable_alpha_to_coverage = false,
                .padding2 = 0,
                .padding3 = 0,
            },
            .depth_stencil_state = {
            },
            .target_info = {
                .color_target_descriptions = color_target_descriptions.data(),
                .num_color_targets = static_cast<Uint32>(color_target_descriptions.size()),
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = false,
                .padding1 = 0,
                .padding2 = 0,
                .padding3 = 0,
            },
            .props = 0,
        };

    auto p = SDL_CreateGPUGraphicsPipeline(device, &createInfo);

    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);

    return p;
}
} // namespace lesl::sdl
