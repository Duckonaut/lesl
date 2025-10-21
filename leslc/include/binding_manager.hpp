#pragma once

#include "spirv/1.0/spirv.hpp"

#include "spirv_binary_container.hpp"

#include "repr.hpp"

#include "codegen_helpers.hpp"

struct BindingManager final {
    enum class TargetAPI {
        Vulkan,
        // SDL3 Resource Binding Scheme:
        // - Vertex Shaders:
        //   - set 0: sampled textures, followed by storage textures, followed by storage
        //   buffers
        //   - set 1: uniform buffers
        // - Fragment Shaders:
        //   - set 2: sampled textures, followed by storage textures, followed by storage
        //   buffers
        //   - set 3: uniform buffers
        SDL3,
    };

    enum class BindingAllocationMode {
        SingleInputMultipleUniform,
        MultiInput,
    };

    TargetAPI target_api;
    BindingAllocationMode mode;

    uint32_t input_binding = 0;
    uint32_t output_binding = 0;
    uint32_t vk_binding = 0;
    uint32_t sdl3_vertex_big_binding = 0;
    uint32_t sdl3_vertex_uniform_binding = 0;
    uint32_t sdl3_fragment_big_binding = 0;
    uint32_t sdl3_fragment_uniform_binding = 0;

    bool vertex_input_allocated = false;
    bool fragment_input_allocated = false;
    bool vertex_input_decorated = false;
    bool fragment_input_decorated = false;

    BindingManager(TargetAPI target_api, BindingAllocationMode mode)
        : target_api(target_api), mode(mode) {}

    void decorate(
        spv_binary::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id,
        bool input
    ) {
        if (input) {
            switch (mode) {
                case BindingAllocationMode::SingleInputMultipleUniform:
                    if ((context == PipelineStage::Vertex && vertex_input_decorated) ||
                        (context == PipelineStage::Fragment && fragment_input_decorated)) {
                        decorate_as_uniform(spv, struct_id);
                    } else {
                        decorate_as_input(spv, s, struct_id);
                        if (context == PipelineStage::Vertex) {
                            vertex_input_decorated = true;
                        } else if (context == PipelineStage::Fragment) {
                            fragment_input_decorated = true;
                        }
                    }
                    break;
                case BindingAllocationMode::MultiInput:
                    decorate_as_input(spv, s, struct_id);
                    break;
            }
        } else {
            decorate_as_output(spv, s, struct_id);
        }
    }

    void decorate_as_input(
        spv_binary::BinaryContainer& spv,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        uint32_t location = 0;

        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);

        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            location++;
        }

        input_binding++;
    }

    void decorate_as_output(
        spv_binary::BinaryContainer& spv,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        uint32_t location = 0;
        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            location++;
        }

        output_binding++;
    }

    void decorate_as_uniform(
        spv_binary::BinaryContainer& spv,
        uint32_t struct_id
    ) {
        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
    }

    void allocate_interfaces(
        spv_binary::BinaryContainer& spv,
        std::vector<GlobalInterface>& interfaces
    ) {
        for (GlobalInterface& gi : interfaces) {
            if (gi.storage_class == StorageClass::Uniform) {
                allocate_as_uniform(spv, gi);
            }
        }
    }

    void allocate_as_uniform(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::Uniform) {
            if (target_api == TargetAPI::SDL3) {
                if (gi.pipeline_stage == PipelineStage::Fragment) {
                    uint32_t set = 3;

                    spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                    spv.Decorate(
                        gi.id,
                        spv::DecorationBinding,
                        &sdl3_fragment_uniform_binding,
                        1
                    );

                    sdl3_fragment_uniform_binding++;
                    return;
                } else {
                    uint32_t set = 1;

                    spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                    spv.Decorate(
                        gi.id,
                        spv::DecorationBinding,
                        &sdl3_vertex_uniform_binding,
                        1
                    );

                    sdl3_vertex_uniform_binding++;
                }
            } else if (target_api == TargetAPI::Vulkan) {
                uint32_t set = 0;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &vk_binding, 1);

                vk_binding++;
            }
        }
    }

    void
    allocate_variable(spv_binary::BinaryContainer& spv, GlobalInterface& gi) {
        uint32_t pointer_type =
            gi.type->get_pointer_type((spv::StorageClass)(uint32_t)gi.storage_class);
        gi.pointer_type = pointer_type;
        spv.Variable(pointer_type, gi.id, (uint32_t)gi.storage_class);
    }

    StorageClass get_input_storage_class(PipelineStage stage) {
        switch (stage) {
            case PipelineStage::Vertex:
                if (vertex_input_allocated) {
                    return StorageClass::Uniform;
                } else {
                    vertex_input_allocated = true;
                    return StorageClass::Input;
                }
                break;
            case PipelineStage::Fragment:
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
};
