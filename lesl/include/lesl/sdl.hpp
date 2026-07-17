#pragma once

#include <SDL3/SDL.h>
#include <lesl/lesl.hpp>

namespace lesl::sdl {
struct SDL3BindingManager : public BindingManagerInterface {
    enum class BindingAllocationMode {
        SingleInputMultipleUniform,
        MultiInput,
    };

    BindingAllocationMode mode;

    uint32_t vertex_input_binding = 0;
    uint32_t vertex_output_binding = 0;
    uint32_t fragment_input_binding = 0;
    uint32_t fragment_output_binding = 0;
    uint32_t sdl3_vertex_big_binding = 0;
    uint32_t sdl3_vertex_uniform_binding = 0;
    uint32_t sdl3_fragment_big_binding = 0;
    uint32_t sdl3_fragment_uniform_binding = 0;

    bool vertex_input_allocated = false;
    bool fragment_input_allocated = false;
    bool vertex_input_decorated = false;
    bool fragment_input_decorated = false;

    std::vector<uint32_t> already_decorated_block;
    std::vector<uint32_t> already_decorated_locations;

    std::vector<Binding> bindings;

    SDL3BindingManager(BindingAllocationMode mode) : mode(mode) {}

    void try_decorate_block(spvbc::BinaryContainer& spv, uint32_t struct_id) {
        if (std::find(
                already_decorated_block.begin(),
                already_decorated_block.end(),
                struct_id
            ) == already_decorated_block.end()) {
            spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
            already_decorated_block.push_back(struct_id);
        }
    }

    void try_decorate_locations(
        spvbc::BinaryContainer& spv,
        const lesl::Decl::Struct& s,
        uint32_t struct_id
    ) {
        if (std::find(
                already_decorated_locations.begin(),
                already_decorated_locations.end(),
                struct_id
            ) == already_decorated_locations.end()) {
            for (uint32_t i = 0; i < s.members.size(); i++) {
                spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &i, 1);
            }
            already_decorated_locations.push_back(struct_id);
        }
    }

    virtual void decorate_struct(
        spvbc::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id,
        bool input
    ) override {
        if (input) {
            switch (mode) {
                case BindingAllocationMode::SingleInputMultipleUniform:
                    if ((context == PipelineStage::Vertex && vertex_input_decorated) ||
                        (context == PipelineStage::Fragment && fragment_input_decorated)) {
                        decorate_as_uniform(spv, struct_id);
                    } else {
                        decorate_as_input(spv, context, s, struct_id);
                        if (context == PipelineStage::Vertex) {
                            vertex_input_decorated = true;
                        } else if (context == PipelineStage::Fragment) {
                            fragment_input_decorated = true;
                        }
                    }
                    break;
                case BindingAllocationMode::MultiInput:
                    decorate_as_input(spv, context, s, struct_id);
                    break;
            }
        } else {
            decorate_as_output(spv, context, s, struct_id);
        }
    }

    void decorate_as_input(
        spvbc::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        try_decorate_block(spv, struct_id);
        try_decorate_locations(spv, s, struct_id);

        for (uint32_t i = 0; i < s.members.size(); i++) {
            auto& rt = *s.members[i].type.resolved_type;
            bindings.push_back(
                Binding{
                    .stage = PipelineStage::Vertex,
                    .type = BindType::Input,
                    .name = s.name.name.to_string() + "::" + s.members[i].name.name.c_str(),
                    .set = context == PipelineStage::Vertex ? vertex_input_binding
                                                            : fragment_input_binding,
                    .slot = i,
                    .size = rt->size,
                    .alignment = rt->alignment,
                    .binding_type = rt->name.to_string(),
                }
            );
        }

        if (context == PipelineStage::Vertex) {
            vertex_input_binding++;
        } else if (context == PipelineStage::Fragment) {
            fragment_input_binding++;
        }
    }

    void decorate_as_output(
        spvbc::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        try_decorate_block(spv, struct_id);
        try_decorate_locations(spv, s, struct_id);
        for (uint32_t i = 0; i < s.members.size(); i++) {
            auto& rt = *s.members[i].type.resolved_type;
            bindings.push_back(
                Binding{
                    .stage = PipelineStage::Vertex,
                    .type = BindType::Output,
                    .name = s.name.name.to_string() + "::" + s.members[i].name.name.c_str(),
                    .set = context == PipelineStage::Vertex ? vertex_output_binding
                                                            : fragment_output_binding,
                    .slot = i,
                    .size = rt->size,
                    .alignment = rt->alignment,
                    .binding_type = rt->name.to_string(),
                }
            );
        }

        if (context == PipelineStage::Vertex) {
            vertex_output_binding++;
        } else if (context == PipelineStage::Fragment) {
            fragment_output_binding++;
        }
    }

    void decorate_as_uniform(spvbc::BinaryContainer& spv, uint32_t struct_id) {
        try_decorate_block(spv, struct_id);
    }

    virtual void decorate_interfaces(
        spvbc::BinaryContainer& spv,
        std::vector<GlobalInterface>& interfaces
    ) override {
        for (GlobalInterface& gi : interfaces) {
            if (gi.storage_class == StorageClass::Uniform) {
                allocate_as_uniform(spv, gi);
            } else if (gi.storage_class == StorageClass::ImageSampler) {
                allocate_as_image_sampler(spv, gi);
            }
        }
    }

    void allocate_as_uniform(spvbc::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::Uniform) {
            if (gi.pipeline_stage == PipelineStage::Fragment) {
                uint32_t set = 3;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_fragment_uniform_binding, 1);

                bindings.push_back(
                    Binding{
                        .stage = PipelineStage::Fragment,
                        .type = BindType::Uniform,
                        .name = gi.name.to_string(),
                        .set = set,
                        .slot = sdl3_fragment_uniform_binding,
                        .size = gi.type->size,
                        .alignment = gi.type->alignment,
                        .binding_type = gi.type->name.to_string(),
                    }
                );

                sdl3_fragment_uniform_binding++;
                return;
            } else {
                uint32_t set = 1;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_vertex_uniform_binding, 1);

                bindings.push_back(
                    Binding{
                        .stage = PipelineStage::Vertex,
                        .type = BindType::Uniform,
                        .name = gi.name.to_string(),
                        .set = set,
                        .slot = sdl3_vertex_uniform_binding,
                        .size = gi.type->size,
                        .alignment = gi.type->alignment,
                        .binding_type = gi.type->name.to_string(),
                    }
                );

                sdl3_vertex_uniform_binding++;
            }
        }
    }

    void allocate_as_image_sampler(spvbc::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::ImageSampler) {
            if (gi.pipeline_stage == PipelineStage::Fragment) {
                uint32_t set = 2;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_fragment_big_binding, 1);

                bindings.push_back(
                    Binding{
                        .stage = PipelineStage::Fragment,
                        .type = BindType::Sampler,
                        .name = gi.name.to_string(),
                        .set = set,
                        .slot = sdl3_fragment_big_binding,
                        .size = gi.type->size,
                        .alignment = gi.type->alignment,
                        .binding_type = gi.type->name.to_string(),
                    }
                );

                sdl3_fragment_big_binding++;
                return;
            } else {
                uint32_t set = 0;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_vertex_big_binding, 1);

                bindings.push_back(
                    Binding{
                        .stage = PipelineStage::Vertex,
                        .type = BindType::Sampler,
                        .name = gi.name.to_string(),
                        .set = set,
                        .slot = sdl3_vertex_big_binding,
                        .size = gi.type->size,
                        .alignment = gi.type->alignment,
                        .binding_type = gi.type->name.to_string(),
                    }
                );

                sdl3_vertex_big_binding++;
            }
        }
    }

    virtual void allocate_interface_variables(
        spvbc::BinaryContainer& spv,
        std::vector<GlobalInterface>& gis
    ) override {
        for (GlobalInterface& gi : gis) {
            uint32_t pointer_type =
                gi.type->get_pointer_type((spv::StorageClass)(uint32_t)gi.storage_class);
            gi.pointer_type = pointer_type;
            spv.Variable(pointer_type, gi.id, (uint32_t)gi.storage_class);
        }
    }

    virtual StorageClass
    get_input_storage_class(const TypeInfo& type_info, PipelineStage stage) override {
        switch (stage) {
            case PipelineStage::Vertex:
                if (type_info.is<TypeInfo::ImageSampler>()) {
                    return StorageClass::ImageSampler;
                }

                if (vertex_input_allocated) {
                    return StorageClass::Uniform;
                } else {
                    vertex_input_allocated = true;
                    return StorageClass::Input;
                }
                break;
            case PipelineStage::Fragment:
                if (type_info.is<TypeInfo::ImageSampler>()) {
                    return StorageClass::ImageSampler;
                }

                if (fragment_input_allocated) {
                    return StorageClass::Uniform;
                } else {
                    fragment_input_allocated = true;
                    return StorageClass::Input;
                }
                break;
        }
        assert(false);
    }

    virtual std::vector<Binding> get_bindings() const override {
        return bindings;
    }
};

SDL_GPUGraphicsPipeline* create_graphics_pipeline(
    SDL_GPUDevice* device,
    CompilationResult cr,
    std::vector<SDL_GPUTextureFormat> color_target_formats,
    std::optional<SDL_GPUTextureFormat> depth_stencil_target_format = std::nullopt,
    std::vector<SDL_GPUVertexAttribute>* vertex_attributes = nullptr,
    std::vector<SDL_GPUVertexBufferDescription>* vertex_buffer_descriptions = nullptr
);
} // namespace lesl::sdl
