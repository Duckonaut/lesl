#pragma once

#include "spirv/1.0/spirv.hpp"

#include "spirv_binary_container.hpp"

#include "lesl/repr.hpp"

#include "codegen_helpers.hpp"

/// Defines an interface for managing resource bindings in SPIR-V code generation.
/// The interface allows for different strategies of binding allocation and decoration
/// based on the needs of the application.
///
/// The decoration and allocation methods are separate as the SPIR-V specification
/// requires theem to be present in different locations in the binary.
struct BindingManagerInterface {
    /// The main method that decorates input/output/uniform/storage structs with the appropriate
    /// decorations based on the pipeline stage and whether it's an input or output.
    virtual void decorate_struct(
        spv_binary::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id,
        bool input
    ) = 0;

    /// Allocates bindings for the provided global interfaces in the SPIR-V binary.
    virtual void decorate_interfaces(
        spv_binary::BinaryContainer& spv,
        std::vector<GlobalInterface>& interfaces
    ) = 0;

    /// Allocates the variables for the provided global interfaces
    virtual void allocate_interface_variables(
        spv_binary::BinaryContainer& spv,
        std::vector<GlobalInterface>& gi
    ) = 0;

    /// Determines the storage class for an input variable based on its type and the pipeline
    /// stage.
    virtual StorageClass
    get_input_storage_class(const TypeInfo& type_info, PipelineStage stage) = 0;
};

// basic binding manager that puts all bindings sequentially in one set
struct SimpleBindingManager : public BindingManagerInterface {
    enum class BindingAllocationMode {
        SingleInputMultipleUniform,
        MultiInput,
    };

    BindingAllocationMode mode;

    uint32_t vk_binding = 0;

    bool vertex_input_allocated = false;
    bool fragment_input_allocated = false;
    bool vertex_input_decorated = false;
    bool fragment_input_decorated = false;

    SimpleBindingManager(BindingAllocationMode mode) : mode(mode) {}

    virtual void decorate_struct(
        spv_binary::BinaryContainer& spv,
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
    }

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
    }

    virtual void decorate_interfaces(
        spv_binary::BinaryContainer& spv,
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

    void allocate_as_uniform(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::Uniform) {
            uint32_t set = 0;

            spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
            spv.Decorate(gi.id, spv::DecorationBinding, &vk_binding, 1);

            vk_binding++;
        }
    }

    void
    allocate_as_image_sampler(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::ImageSampler) {
            uint32_t set = 0;

            spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
            spv.Decorate(gi.id, spv::DecorationBinding, &vk_binding, 1);

            vk_binding++;
        }
    }

    virtual void allocate_interface_variables(
        spv_binary::BinaryContainer& spv,
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
};

struct SDL3BindingManager : public BindingManagerInterface {
    enum class BindingAllocationMode {
        SingleInputMultipleUniform,
        MultiInput,
    };

    BindingAllocationMode mode;

    uint32_t input_binding = 0;
    uint32_t output_binding = 0;
    uint32_t sdl3_vertex_big_binding = 0;
    uint32_t sdl3_vertex_uniform_binding = 0;
    uint32_t sdl3_fragment_big_binding = 0;
    uint32_t sdl3_fragment_uniform_binding = 0;

    bool vertex_input_allocated = false;
    bool fragment_input_allocated = false;
    bool vertex_input_decorated = false;
    bool fragment_input_decorated = false;

    SDL3BindingManager(BindingAllocationMode mode) : mode(mode) {}

    virtual void decorate_struct(
        spv_binary::BinaryContainer& spv,
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

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
    }

    virtual void decorate_interfaces(
        spv_binary::BinaryContainer& spv,
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

    void allocate_as_uniform(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::Uniform) {
            if (gi.pipeline_stage == PipelineStage::Fragment) {
                uint32_t set = 3;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_fragment_uniform_binding, 1);

                sdl3_fragment_uniform_binding++;
                return;
            } else {
                uint32_t set = 1;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_vertex_uniform_binding, 1);

                sdl3_vertex_uniform_binding++;
            }
        }
    }

    void
    allocate_as_image_sampler(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
        if (gi.storage_class == StorageClass::ImageSampler) {
            if (gi.pipeline_stage == PipelineStage::Fragment) {
                uint32_t set = 2;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_fragment_big_binding, 1);

                sdl3_fragment_big_binding++;
                return;
            } else {
                uint32_t set = 0;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &sdl3_vertex_big_binding, 1);

                sdl3_vertex_big_binding++;
            }
        }
    }

    virtual void allocate_interface_variables(
        spv_binary::BinaryContainer& spv,
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
};

struct DictionaryBindingManager : public BindingManagerInterface {
    struct InterfaceBinding {
        StorageClass storage_class;
        uint32_t set;
        uint32_t binding;
    };

    std::unordered_map<PoolStr, InterfaceBinding> binding_dict;

    DictionaryBindingManager(const std::unordered_map<PoolStr, InterfaceBinding>& dict)
        : binding_dict(dict) {}

    virtual void decorate_struct(
        spv_binary::BinaryContainer& spv,
        PipelineStage,
        const Decl::Struct& s,
        uint32_t struct_id,
        bool
    ) override {
        InterfaceBinding& ib = binding_dict.at(s.name.name);

        if (ib.storage_class == StorageClass::Uniform) {
            spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
        } else {
            uint32_t location = 0;
            spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
            for (uint32_t i = 0; i < s.members.size(); i++) {
                spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
                location++;
            }
        }
    }

    virtual void decorate_interfaces(
        spv_binary::BinaryContainer& spv,
        std::vector<GlobalInterface>& interfaces
    ) override {
        for (GlobalInterface& gi : interfaces) {
            InterfaceBinding& ib = binding_dict.at(gi.name);

            if (ib.storage_class == StorageClass::Uniform ||
                ib.storage_class == StorageClass::ImageSampler) {
                uint32_t set = ib.set;
                uint32_t binding = ib.binding;

                spv.Decorate(gi.id, spv::DecorationDescriptorSet, &set, 1);
                spv.Decorate(gi.id, spv::DecorationBinding, &binding, 1);
            }
        }
    }

    virtual void allocate_interface_variables(
        spv_binary::BinaryContainer& spv,
        std::vector<GlobalInterface>& gi
    ) override {
        for (GlobalInterface& gi : gi) {
            uint32_t pointer_type =
                gi.type->get_pointer_type((spv::StorageClass)(uint32_t)gi.storage_class);
            gi.pointer_type = pointer_type;
            spv.Variable(pointer_type, gi.id, (uint32_t)gi.storage_class);
        }
    }

    virtual StorageClass
    get_input_storage_class(const TypeInfo& type_info, PipelineStage stage) override {
        InterfaceBinding& ib = binding_dict.at(type_info.name);

        return ib.storage_class;
    }
};
