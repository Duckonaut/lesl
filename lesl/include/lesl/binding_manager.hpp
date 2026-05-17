#pragma once

#include "spirv/1.0/spirv.hpp"

#include "spirv_binary_container.hpp"

#include "lesl/repr.hpp"

#include "codegen_helpers.hpp"
#include <algorithm>

namespace lesl {
enum class BindType {
    Input,
    Output,
    Sampler,
    Uniform,
};
struct Binding {
    PipelineStage stage;
    BindType type;
    std::string name;
    uint32_t set;
    uint32_t slot;
    uint32_t size;
    uint32_t alignment;
    std::string binding_type;
};

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

    virtual std::vector<Binding> get_bindings() const = 0;

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

    std::vector<uint32_t> already_decorated_block;

    std::vector<Binding> bindings;

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

    void try_decorate_block(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        if (std::find(
                already_decorated_block.begin(),
                already_decorated_block.end(),
                struct_id
            ) == already_decorated_block.end()) {
            spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
            already_decorated_block.push_back(struct_id);
        }
    }

    void decorate_as_input(
        spv_binary::BinaryContainer& spv,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        uint32_t location = 0;

        try_decorate_block(spv, struct_id);

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
        try_decorate_block(spv, struct_id);
        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            location++;
        }
    }

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        try_decorate_block(spv, struct_id);
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

    virtual std::vector<Binding> get_bindings() const override {
        return bindings;
    }
};

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

    std::vector<Binding> bindings;

    SDL3BindingManager(BindingAllocationMode mode) : mode(mode) {}

    void try_decorate_block(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        if (std::find(
                already_decorated_block.begin(),
                already_decorated_block.end(),
                struct_id
            ) == already_decorated_block.end()) {
            spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
            already_decorated_block.push_back(struct_id);
        }
    }

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
        spv_binary::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        uint32_t location = 0;

        try_decorate_block(spv, struct_id);

        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            auto& rt = *s.members[i].type.resolved_type;
            bindings.push_back(
                Binding{
                    .stage = PipelineStage::Vertex,
                    .type = BindType::Input,
                    .name = s.name.name.to_string() + "::" + s.members[i].name.name.c_str(),
                    .set = context == PipelineStage::Vertex ? vertex_input_binding
                                                            : fragment_input_binding,
                    .slot = location,
                    .size = rt->size,
                    .alignment = rt->alignment,
                    .binding_type = rt->name.to_string(),
                }
            );
            location++;
        }

        if (context == PipelineStage::Vertex) {
            vertex_input_binding++;
        } else if (context == PipelineStage::Fragment) {
            fragment_input_binding++;
        }
    }

    void decorate_as_output(
        spv_binary::BinaryContainer& spv,
        PipelineStage context,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        uint32_t location = 0;
        try_decorate_block(spv, struct_id);
        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);

            auto& rt = *s.members[i].type.resolved_type;
            bindings.push_back(
                Binding{
                    .stage = PipelineStage::Vertex,
                    .type = BindType::Output,
                    .name = s.name.name.to_string() + "::" + s.members[i].name.name.c_str(),
                    .set = context == PipelineStage::Vertex ? vertex_output_binding
                                                            : fragment_output_binding,
                    .slot = location,
                    .size = rt->size,
                    .alignment = rt->alignment,
                    .binding_type = rt->name.to_string(),
                }
            );
            location++;
        }

        if (context == PipelineStage::Vertex) {
            vertex_output_binding++;
        } else if (context == PipelineStage::Fragment) {
            fragment_output_binding++;
        }
    }

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, uint32_t struct_id) {
        try_decorate_block(spv, struct_id);
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

    void
    allocate_as_image_sampler(spv_binary::BinaryContainer& spv, const GlobalInterface& gi) {
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

    virtual std::vector<Binding> get_bindings() const override {
        return bindings;
    }
};

struct DictionaryBindingManager : public BindingManagerInterface {
    struct InterfaceBinding {
        std::string name;
        PipelineStage stage;
        StorageClass storage_class;
        uint32_t set;
        uint32_t slot;
    };

    std::vector<InterfaceBinding> dict;

    std::vector<Binding> bindings;

    DictionaryBindingManager(const std::vector<InterfaceBinding>& dict) : dict(dict) {}

    Opt<InterfaceBinding> get_binding(PoolStr b) {
        auto it = std::find_if(dict.begin(), dict.end(), [&b](const InterfaceBinding& bd) {
            return b == bd.name;
        });

        if (it != dict.end()) {
            return *it;
        }
        return std::nullopt;
    }

    virtual void decorate_struct(
        spv_binary::BinaryContainer& spv,
        PipelineStage,
        const Decl::Struct& s,
        uint32_t struct_id,
        bool
    ) override {
        Opt<InterfaceBinding> oib = get_binding(s.name.name);

        if (!oib) {
            return;
        }
        InterfaceBinding ib = *oib;

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
            Opt<InterfaceBinding> oib = get_binding(gi.name);

            if (!oib) {
                return;
            }
            InterfaceBinding ib = *oib;

            if (ib.storage_class == StorageClass::Uniform ||
                ib.storage_class == StorageClass::ImageSampler) {
                uint32_t set = ib.set;
                uint32_t binding = ib.slot;

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
    get_input_storage_class(const TypeInfo& type_info, PipelineStage) override {
        Opt<InterfaceBinding> oib = get_binding(type_info.name);

        if (!oib) {
            return StorageClass::Uniform;
        }

        return oib->storage_class;
    }

    virtual std::vector<Binding> get_bindings() const override {
        return bindings;
    }
};
} // namespace lesl
