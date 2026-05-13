#include <SDL3/SDL.h>

#include <cmath>
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

SDL_GPUPrimitiveType parse_primitive(const char* s) {
    static std::unordered_map<std::string_view, SDL_GPUPrimitiveType> prims = {
        { "TriangleList", SDL_GPU_PRIMITIVETYPE_TRIANGLELIST },
        { "TriangleStrip", SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP },
        { "LineList", SDL_GPU_PRIMITIVETYPE_LINELIST },
        { "LineStrip", SDL_GPU_PRIMITIVETYPE_LINESTRIP },
        { "PointList", SDL_GPU_PRIMITIVETYPE_POINTLIST },
    };

    return prims[s];
}

SDL_GPUCullMode parse_cull_mode(const char* s) {
    static std::unordered_map<std::string_view, SDL_GPUCullMode> cull_modes = {
        { "None", SDL_GPU_CULLMODE_NONE },
        { "Off", SDL_GPU_CULLMODE_NONE },
        { "Front", SDL_GPU_CULLMODE_FRONT },
        { "Back", SDL_GPU_CULLMODE_BACK },
    };

    return cull_modes[s];
}

SDL_GPUFillMode parse_fill_mode(const char* s) {
    static std::unordered_map<std::string_view, SDL_GPUFillMode> fill_mode = {
        { "Fill", SDL_GPU_FILLMODE_FILL },
        { "Line", SDL_GPU_FILLMODE_LINE },
    };

    return fill_mode[s];
}

SDL_GPUFrontFace parse_front_face(const char* s) {
    static std::unordered_map<std::string_view, SDL_GPUFrontFace> front_faces = {
        { "Clockwise", SDL_GPU_FRONTFACE_CLOCKWISE },
        { "CW", SDL_GPU_FRONTFACE_CLOCKWISE },
        { "CounterClockwise", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
        { "CCW", SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
    };

    return front_faces[s];
}

bool parse_bool(const char* s, bool default_value) {
    std::string_view ss = s;
    if (ss == "True" || ss == "true" || ss == "On" || ss == "on") {
        return true;
    } else if (ss == "False" || ss == "false" || ss == "Off" || ss == "off") {
        return false;
    }
    return default_value;
}

SDL_GPUCompareOp parse_compare_op(const char* s, SDL_GPUCompareOp default_op) {
    static std::unordered_map<std::string_view, SDL_GPUCompareOp> compare_ops = {
        { "Never", SDL_GPU_COMPAREOP_NEVER },
        { "Less", SDL_GPU_COMPAREOP_LESS },
        { "Equal", SDL_GPU_COMPAREOP_EQUAL },
        { "LessEqual", SDL_GPU_COMPAREOP_LESS_OR_EQUAL },
        { "Greater", SDL_GPU_COMPAREOP_GREATER },
        { "GreaterEqual", SDL_GPU_COMPAREOP_GREATER_OR_EQUAL },
        { "NotEqual", SDL_GPU_COMPAREOP_NOT_EQUAL },
        { "Always", SDL_GPU_COMPAREOP_ALWAYS },
    };

    auto v = compare_ops[s];
    if (v == SDL_GPU_COMPAREOP_INVALID) {
        return default_op;
    }
    return compare_ops[s];
}

uint8_t parse_u8(const char* s, uint8_t default_value) {
    std::string_view ss = s;
    if (ss.empty()) {
        return default_value;
    }
    double v = std::stod(s);
    if (std::trunc(v) != v || v < 0.0 || v > 255.0) {
        return default_value;
    }

    return (uint8_t)v;
}

SDL_GPUStencilOp parse_stencil_op(const char* s, SDL_GPUStencilOp default_op) {
    static std::unordered_map<std::string_view, SDL_GPUStencilOp> stencil_ops = {
        { "Keep", SDL_GPU_STENCILOP_KEEP },
        { "Zero", SDL_GPU_STENCILOP_ZERO },
        { "Replace", SDL_GPU_STENCILOP_REPLACE },
        { "IncrementAndClamp", SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP },
        { "DecrementAndClamp", SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP },
        { "Invert", SDL_GPU_STENCILOP_INVERT },
        { "IncrementAndWrap", SDL_GPU_STENCILOP_INCREMENT_AND_WRAP },
        { "DecrementAndWrap", SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP },
    };

    auto v = stencil_ops[s];
    if (v == SDL_GPU_STENCILOP_INVALID) {
        return default_op;
    }
    return stencil_ops[s];
}

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    CompilationResult cr,
    std::vector<SDL_GPUTextureFormat> color_target_formats,
    std::optional<SDL_GPUTextureFormat> depth_stencil_target_format = std::nullopt,
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
        parse_primitive(cr.pipeline_parameters[CONVENTION_PRIMITIVE_TYPE_KEY].c_str());

    SDL_GPURasterizerState raster{};
    raster.cull_mode =
        parse_cull_mode(cr.pipeline_parameters[CONVENTION_CULL_MODE_KEY].c_str());
    raster.fill_mode =
        parse_fill_mode(cr.pipeline_parameters[CONVENTION_FILL_MODE_KEY].c_str());
    raster.front_face =
        parse_front_face(cr.pipeline_parameters[CONVENTION_FRONT_FACE_KEY].c_str());

    SDL_GPUDepthStencilState depth_stencil_state{};
    depth_stencil_state.enable_depth_test =
        parse_bool(cr.pipeline_parameters[CONVENTION_DEPTH_TEST_KEY].c_str(), true);
    depth_stencil_state.enable_depth_write =
        parse_bool(cr.pipeline_parameters[CONVENTION_DEPTH_WRITE_KEY].c_str(), false);
    depth_stencil_state.enable_stencil_test =
        parse_bool(cr.pipeline_parameters[CONVENTION_STENCIL_TEST_KEY].c_str(), false);
    depth_stencil_state.compare_op = parse_compare_op(
        cr.pipeline_parameters[CONVENTION_DEPTH_OP_KEY].c_str(),
        SDL_GPU_COMPAREOP_ALWAYS
    );
    depth_stencil_state.compare_mask = parse_u8(
        cr.pipeline_parameters[CONVENTION_STENCIL_COMPARE_MASK_KEY].c_str(),
        0b11111111
    );
    depth_stencil_state.write_mask =
        parse_u8(cr.pipeline_parameters[CONVENTION_STENCIL_WRITE_MASK_KEY].c_str(), 0b11111111);

    depth_stencil_state.front_stencil_state.compare_op = parse_compare_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_FRONT_OP].c_str(),
        SDL_GPU_COMPAREOP_ALWAYS
    );
    depth_stencil_state.front_stencil_state.pass_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_PASS_FRONT_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );
    depth_stencil_state.front_stencil_state.fail_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_FAIL_FRONT_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );
    depth_stencil_state.front_stencil_state.depth_fail_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_DEPTH_FAIL_FRONT_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );

    depth_stencil_state.back_stencil_state.compare_op = parse_compare_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_BACK_OP].c_str(),
        SDL_GPU_COMPAREOP_ALWAYS
    );
    depth_stencil_state.back_stencil_state.pass_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_PASS_BACK_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );
    depth_stencil_state.back_stencil_state.fail_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_FAIL_BACK_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );
    depth_stencil_state.back_stencil_state.depth_fail_op = parse_stencil_op(
        cr.pipeline_parameters[CONVENTION_STENCIL_DEPTH_FAIL_BACK_OP].c_str(),
        SDL_GPU_STENCILOP_KEEP
    );

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
            .depth_stencil_state = depth_stencil_state,
            .target_info = {
                .color_target_descriptions = color_target_descriptions.data(),
                .num_color_targets = static_cast<Uint32>(color_target_descriptions.size()),
                .depth_stencil_format = depth_stencil_target_format ? *depth_stencil_target_format : SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                .has_depth_stencil_target = depth_stencil_target_format.has_value(),
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
