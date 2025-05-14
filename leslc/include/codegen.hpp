#pragma once

#include "log.hpp"
#include "utils.hpp"
#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include "spirv_binary_container.hpp"

#include "repr.hpp"
#include "arena.hpp"
#include "stringpool.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <ostream>
#include <unordered_map>
#include <variant>

enum class StorageClass : uint32_t {
	Input = spv::StorageClassInput,
	Output = spv::StorageClassOutput,
	Uniform = spv::StorageClassUniform,
	StorageBuffer = spv::StorageClassStorageBuffer,
};

enum class PipelineStage {
	Vertex,
	Fragment,
};

struct GlobalInterface {
    StorageClass storage_class;
    PipelineStage pipeline_stage;

    Ref<TypeInfo> type;
    PoolStr name;
    uint32_t id;
    uint32_t pointer_type;
};

struct BindingManager final {
    enum class TargetAPI {
        Vulkan, // arbitrary binding layout for Vulkan
        SDL3,   // bind resources to the schema expected by SDL3
    };
    enum class BindingAllocationMode {
        SingleInputMultipleUniform,
        MultiInput,
    };

    TargetAPI target_api;
    BindingAllocationMode mode;

    uint32_t binding = 0;
    uint32_t set = 0;
    uint32_t location = 0;

    bool vertex_input_allocated = false;
    bool fragment_input_allocated = false;
    bool vertex_input_decorated = false;
    bool fragment_input_decorated = false;

    BindingManager(TargetAPI target_api, BindingAllocationMode mode) : target_api(target_api), mode(mode) {}

    void decorate(spv_binary::BinaryContainer& spv, PipelineStage context, const Decl::Struct& s, uint32_t struct_id, bool input) {
        if (input) {
        switch (mode) {
            case BindingAllocationMode::SingleInputMultipleUniform:
                if ((context == PipelineStage::Vertex && vertex_input_decorated)
                    || (context == PipelineStage::Fragment && fragment_input_decorated)) {
                    decorate_as_uniform(spv, s, struct_id);
                }
                else {
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

    void decorate_as_input(spv_binary::BinaryContainer& spv, const Decl::Struct& s, uint32_t struct_id) {
        location = 0;

        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);

        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            location++;
        }

        binding++;
    }

    void decorate_as_output(
        spv_binary::BinaryContainer& spv,
        const Decl::Struct& s,
        uint32_t struct_id
    ) {
        location = 0;
        spv.Decorate(struct_id, spv::DecorationBlock, NULL, 0);
        for (uint32_t i = 0; i < s.members.size(); i++) {
            spv.MemberDecorate(struct_id, i, spv::DecorationLocation, &location, 1);
            location++;
        }
        binding++;
    }

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, const Decl::Struct& s, uint32_t struct_id) {
    }

    void
    allocate_variable(spv_binary::BinaryContainer& spv, GlobalInterface& gi, uint32_t type_id) {
        uint32_t pointer_type = gi.type->get_pointer_type((spv::StorageClass)(uint32_t)gi.storage_class);
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

struct VariableInstance {
    uint32_t id;
    Opt<spv::StorageClass> storage_class;
    Opt<Ref<TypeInfo>> type;

    VariableInstance() : id(0), storage_class(std::nullopt), type(std::nullopt) {}
    VariableInstance(uint32_t id, Ref<TypeInfo> type, Opt<spv::StorageClass> storage_class) : id(id), type(type), storage_class(storage_class) {}
};

struct VariableScope {
    std::unordered_map<PoolStr, VariableInstance> variables;
};

struct BuiltinInfo {
    uint32_t id;
    uint32_t type_id;
    uint32_t pointer_type_id;
    spv::StorageClass storage_class;
};

class CodeGenerator final {
  public:
    CompilationArena& arena;
    spv_binary::BinaryContainer spv;

    uint32_t constants_insert_point;
    uint32_t glsl_ext;
    std::unordered_map<PoolStr, uint32_t> decl_ids;
    std::unordered_map<PoolStr, BuiltinInfo> builtins;
    std::unordered_map<int32_t, uint32_t> int_constant_ids;
    std::unordered_map<uint32_t, uint32_t> uint_constant_ids;
    std::unordered_map<float, uint32_t> float_constant_ids;
    std::vector<uint32_t> constant_block;

    std::vector<GlobalInterface> global_interfaces;
    std::vector<VariableScope> variable_scopes;

    BindingManager& binding_manager;

    CodeGenerator(CompilationArena& arena, BindingManager& binding_manager)
        : arena(arena), binding_manager(binding_manager) {}

    void generate() {
        generate_prelude();

        // preallocate decl ids
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                decl_ids[decl->get<Decl::Struct>().name.name] = spv.get_id();
            } else if (decl->is<Decl::Function>()) {
                decl_ids[decl->get<Decl::Function>().name.name] = spv.get_id();
            }
        }

        preallocate_interface_ids();
        preallocate_builtins();
        generate_entry_points();
        generate_debug_info();
        generate_decorations();
        generate_builtins();
        generate_types();
        generate_functions();

        backfill_constants();

        spv.update_bound();

        for (auto& [name, id] : decl_ids) {
            std::cout << colorize::green(name.c_str()) << " -> " << colorize::yellow(id)
                      << std::endl;
        }

        uint32_t i = 5;
        uint32_t opn = 0;
        while (i < spv.words.size()) {
            uint32_t inst = spv.words[i];
            uint32_t word_count = (inst >> 16) & 0xffff;

            std::cout << opn << " " << colorize::cyan("OpID ")
                      << colorize::yellow(inst & 0xffff) << colorize::cyan(" WordCount ")
                      << colorize::yellow(word_count) << ": ";

            for (uint32_t j = 0; j < word_count; j++) {
                printf("%08x ", spv.words[i + j]);
            }

            printf("\n");

            i += word_count;
            opn++;
        }
    }

    void generate_prelude() {
        spv.Capability(spv::CapabilityShader);
        glsl_ext = spv.ExtInstImportNew("GLSL.std.450");
        spv.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    }

    void preallocate_interface_ids() {
        std::vector<PoolStr> fragment_entry_points;
        std::vector<PoolStr> vertex_entry_points;

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Pipeline>()) {
                Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                for (PipelineParameter& param : p.params) {
                    if (param.name.name == "Vertex") {
                        if (std::find(
                                vertex_entry_points.begin(),
                                vertex_entry_points.end(),
                                param.value.name
                            ) == vertex_entry_points.end()) {
                            vertex_entry_points.push_back(param.value.name);
                        }
                    } else if (param.name.name == "Fragment") {
                        if (std::find(
                                fragment_entry_points.begin(),
                                fragment_entry_points.end(),
                                param.value.name
                            ) == fragment_entry_points.end()) {
                            fragment_entry_points.push_back(param.value.name);
                        }
                    }
                }
            }
        }

        std::vector<TypedIdentifier> vertex_inputs;
        std::vector<TypedIdentifier> vertex_outputs;
        std::vector<TypedIdentifier> fragment_inputs;
        std::vector<TypedIdentifier> fragment_outputs;

        for (PoolStr& name : vertex_entry_points) {
            Decl::Function& f = find_function(name);

            for (TypedIdentifier& param : f.params) {
                vertex_inputs.push_back(param);
            }

            for (TypedIdentifier& ret : f.rets) {
                vertex_outputs.push_back(ret);
            }
        }

        for (PoolStr& name : fragment_entry_points) {
            Decl::Function& f = find_function(name);

            for (TypedIdentifier& param : f.params) {
                fragment_inputs.push_back(param);
            }

            for (TypedIdentifier& ret : f.rets) {
                fragment_outputs.push_back(ret);
            }
        }

        for (TypedIdentifier& type : vertex_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({
                binding_manager.get_input_storage_class(PipelineStage::Vertex),
                PipelineStage::Vertex,
                type.type.resolved_type.value(),
                type.name.name,
                id,
                0,
            });
        }

        for (TypedIdentifier& type : vertex_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({
                StorageClass::Output,
                PipelineStage::Vertex,
                type.type.resolved_type.value(),
                type.name.name,
                id,
                0,
            });
        }

        for (TypedIdentifier& type : fragment_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({
                binding_manager.get_input_storage_class(PipelineStage::Fragment),
                PipelineStage::Fragment,
                type.type.resolved_type.value(),
                type.name.name,
                id,
                0,
            });
        }

        for (TypedIdentifier& type : fragment_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({
                StorageClass::Output,
                PipelineStage::Fragment,
                type.type.resolved_type.value(),
                type.name.name,
                id,
                0,
            });
        }
    }

    void preallocate_builtins() {
        uint32_t id = spv.get_id();
        builtins[arena.string_pool.add("POSITION")] = {
            id,
            0,
            0,
        };
    }

    void generate_entry_points() {
        std::vector<PoolStr> fragment_entry_points;
        std::vector<PoolStr> vertex_entry_points;

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Pipeline>()) {
                Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                for (PipelineParameter& param : p.params) {
                    if (param.name.name == "Vertex") {
                        if (std::find(
                                vertex_entry_points.begin(),
                                vertex_entry_points.end(),
                                param.value.name
                            ) == vertex_entry_points.end()) {
                            vertex_entry_points.push_back(param.value.name);
                        }
                    } else if (param.name.name == "Fragment") {
                        if (std::find(
                                fragment_entry_points.begin(),
                                fragment_entry_points.end(),
                                param.value.name
                            ) == fragment_entry_points.end()) {
                            fragment_entry_points.push_back(param.value.name);
                        }
                    }
                }
            }
        }

        for (PoolStr& name : vertex_entry_points) {
            Decl::Function& f = find_function(name);

            std::vector<uint32_t> ops;
            for (TypedIdentifier& param : f.params) {
                ops.push_back(resolve_type(param.type.name.name));
            }

            std::vector<uint32_t> interfaces;

            for (GlobalInterface& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Vertex) {
                    interfaces.push_back(gi.id);
                }
            }

            for (const auto& [_, builtin] : builtins) {
                interfaces.push_back(builtin.id);
            }

            spv.EntryPoint(
                spv::ExecutionModelVertex,
                decl_ids[name],
                name.c_str(),
                interfaces.data(),
                interfaces.size()
            );
        }

        for (PoolStr& name : fragment_entry_points) {
            Decl::Function& f = find_function(name);

            std::vector<uint32_t> ops;
            for (TypedIdentifier& param : f.params) {
                ops.push_back(resolve_type(param.type.name.name));
            }

            std::vector<uint32_t> interfaces;

            for (GlobalInterface& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Fragment) {
                    interfaces.push_back(gi.id);
                }
            }

            spv.EntryPoint(
                spv::ExecutionModelFragment,
                decl_ids[name],
                name.c_str(),
                interfaces.data(),
                interfaces.size()
            );

            spv.ExecutionMode(decl_ids[name], spv::ExecutionModeOriginUpperLeft);
        }
    }

    void generate_debug_info() {
        spv.Source(0, 0);
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                generate_struct_debug_info(decl->get<Decl::Struct>());
            }
        }
    }

    void generate_decorations() {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                generate_struct_decorations(decl->get<Decl::Struct>());
            }
        }

        BuiltinInfo& position = builtins[arena.string_pool.add("POSITION")];
        uint32_t builtin = spv::BuiltInPosition;
        spv.Decorate(position.id, spv::DecorationBuiltIn, &builtin, 1);
    }

    void generate_builtins() {
        decl_ids[arena.string_pool.add("void")] = spv.TypeVoidNew();
        decl_ids[arena.string_pool.add("bool")] = spv.TypeBoolNew();
        decl_ids[arena.string_pool.add("int")] = spv.TypeIntNew(32, 1);
        decl_ids[arena.string_pool.add("uint")] = spv.TypeIntNew(32, 0);
        decl_ids[arena.string_pool.add("float")] = spv.TypeFloatNew(32);

        Opt<Ref<TypeInfo>> float4_type_info_ref;

        for (Ref<TypeInfo> type : arena.types) {
            if (type->is<TypeInfo::Vector>()) {
                TypeInfo::Vector& v = type->get<TypeInfo::Vector>();
                if (v.size == 4 && v.element->name == "float") {
                    float4_type_info_ref = type;
                    break;
                }
            }
        }

        assert(float4_type_info_ref.has_value());

        uint32_t float4_id = spv.TypeVectorNew(decl_ids[arena.string_pool.add("float")], 4);
        decl_ids[arena.string_pool.add("float4")] = float4_id;
            

        uint32_t float4_output_ptr = spv.TypePointerNew(spv::StorageClassOutput, float4_id);
        float4_type_info_ref.value()->add_pointer_type(
            spv::StorageClassOutput,
            float4_output_ptr
        );

        BuiltinInfo& position = builtins[arena.string_pool.add("POSITION")];
        spv.Variable(
            float4_output_ptr,
            position.id,
            spv::StorageClassOutput
        );
        position.type_id = float4_id;
        position.pointer_type_id = float4_output_ptr;
        position.storage_class = spv::StorageClassOutput;
    }

    void constant_encode(uint32_t id_result_type, uint32_t id_result, uint32_t value) {
        uint32_t op = 43;
        uint32_t operand_count = 4;
        op |= operand_count << 16;
        constant_block.push_back(op);
        constant_block.push_back(id_result_type);
        constant_block.push_back(id_result);
        constant_block.push_back(value);
    }

    uint32_t get_constant_int(int32_t value) {
        auto it = int_constant_ids.find(value);
        if (it != int_constant_ids.end()) {
            return it->second;
        }

        uint32_t id = spv.get_id();
        int_constant_ids[value] = id;
        constant_encode(decl_ids[arena.string_pool.add("int")], id, std::bit_cast<uint32_t>(value));
        return id;
    }

    uint32_t get_constant_uint(uint32_t value) {
        auto it = uint_constant_ids.find(value);
        if (it != uint_constant_ids.end()) {
            return it->second;
        }

        uint32_t id = spv.get_id();
        uint_constant_ids[value] = id;
        constant_encode(decl_ids[arena.string_pool.add("uint")], id, value);
        return id;
    }

    uint32_t get_constant_float(float value) {
        auto it = float_constant_ids.find(value);
        if (it != float_constant_ids.end()) {
            return it->second;
        }

        uint32_t id = spv.get_id();
        float_constant_ids[value] = id;
        constant_encode(decl_ids[arena.string_pool.add("float")], id, std::bit_cast<uint32_t>(value));
        return id;
    }

    void backfill_constants() {
        spv.insert(constant_block, constants_insert_point);
    }

    uint32_t try_add_storage_class_pointer(
        TypeInfo& type_info,
        spv::StorageClass storage_class
    ) {
        uint32_t id = 0;
        if (type_info.pointer_type_ids.find(static_cast<uint32_t>(storage_class)) ==
            type_info.pointer_type_ids.end()) {
            id = spv.TypePointerNew(storage_class, type_info.id);
            type_info.add_pointer_type(storage_class, id);
        } else {
            id = type_info.get_pointer_type(storage_class);
        }

        return id;
    }

    void generate_type_from_info(TypeInfo& type_info) {
        std::visit(
            overloaded{
                [&](const TypeInfo::Primitive& p) {
                    type_info.id = decl_ids[type_info.name];
                    if (p.primitive != TypeInfo::BuiltinPrimitive::Void) {
                        try_add_storage_class_pointer(type_info, spv::StorageClassFunction);
                        try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                        try_add_storage_class_pointer(type_info, spv::StorageClassOutput);
                        try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                    }
                },
                [&](const TypeInfo::Vector& v) {
                    uint32_t underlying_type_id = resolve_type(v.element->name);

                    if (!(v.size == 4 && v.element->name == "float")) {
                        uint32_t type_id = spv.TypeVectorNew(underlying_type_id, v.size);
                        decl_ids[type_info.name] = type_id;
                    }
                    type_info.id = decl_ids[type_info.name];
                    try_add_storage_class_pointer(type_info, spv::StorageClassFunction);
                    try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassOutput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                },
                [&](const TypeInfo::Matrix& m) {
                    uint32_t underlying_type_id = resolve_type(m.vector_element->name);
                    uint32_t type_id = spv.TypeMatrixNew(underlying_type_id, m.columns);
                    decl_ids[type_info.name] = type_id;

                    type_info.id = decl_ids[type_info.name];
                    try_add_storage_class_pointer(type_info, spv::StorageClassFunction);
                    try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassOutput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                },
                [&](const TypeInfo::Struct& s) {
                    generate_struct(type_info.name, s);

                    type_info.id = decl_ids[type_info.name];

                    try_add_storage_class_pointer(type_info, spv::StorageClassFunction);
                    try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassOutput);
                    try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                },
                [&](const TypeInfo::Array& a) {
                    uint32_t underlying_type_id = resolve_type(a.element->name);
                    if (a.is_sized) {
                        uint32_t type_id =
                            spv.TypeArrayNew(underlying_type_id, get_constant_int(a.size));
                        decl_ids[type_info.name] = type_id;

                        type_info.id = decl_ids[type_info.name];

                        try_add_storage_class_pointer(type_info, spv::StorageClassFunction);
                        try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                        try_add_storage_class_pointer(type_info, spv::StorageClassOutput);
                        try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                    } else {
                        uint32_t type_id = spv.TypeRuntimeArrayNew(underlying_type_id);
                        decl_ids[type_info.name] = type_id;

                        type_info.id = decl_ids[type_info.name];

                        try_add_storage_class_pointer(type_info, spv::StorageClassInput);
                        try_add_storage_class_pointer(type_info, spv::StorageClassUniform);
                    }
                },
            },
            type_info.data
        );
    }

    Opt<Ref<TypeInfo>> get_type_info(const std::string& name) {
        for (Ref<TypeInfo> ti : arena.types) {
            if (ti->name.c_str() == name) {
                return ti;
            }
        }
        return std::nullopt;
    }

    void generate_types() {
        constants_insert_point = spv.size();
        for (Ref<TypeInfo> ti : arena.types) {
            generate_type_from_info(*ti);
        }

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                generate_function_types(decl->get<Decl::Function>());
            }
        }

        for (GlobalInterface& gi : global_interfaces) {
            uint32_t type_id = resolve_type(gi.type->name);

            binding_manager.allocate_variable(spv, gi, type_id);
        }
    }

    void generate_struct_debug_info(const Decl::Struct& s) {
        int32_t n_ops = 0;

        for (const TypedIdentifier& member : s.members) {
            spv.MemberName(decl_ids[s.name.name], n_ops, member.name.name.c_str());

            n_ops++;
        }

        spv.Name(decl_ids[s.name.name], s.name.name.c_str());
    }

    uint32_t get_type_size_offset(uint32_t previous_offset, const TypeInfo& type_info) {
        uint32_t size = type_info.size;
        uint32_t alignment = type_info.alignment;

        if (size % alignment != 0) {
            size += alignment - (size % alignment);
        }

        return size;
    }

    void generate_struct_decorations(const Decl::Struct& s) {
        int32_t n_ops = 0;

        uint32_t offset = 0;

        for (const TypedIdentifier& member : s.members) {
            spv.MemberDecorate(decl_ids[s.name.name], n_ops, spv::DecorationOffset, &offset, 1);

            offset += get_type_size_offset(offset, **member.type.resolved_type);
            n_ops++;
        }

        bool is_vertex_input_interface = false;
        bool is_fragment_input_interface = false;
        bool is_vertex_output_interface = false;
        bool is_fragment_output_interface = false;

        for (const auto& intf : global_interfaces) {
            if (intf.type == s.resolved_type && intf.pipeline_stage == PipelineStage::Vertex) {
                if (intf.storage_class == StorageClass::Output) {
                    is_vertex_output_interface = true;
                } else {
                    is_vertex_input_interface = true;
                }
            } else if (intf.type == s.resolved_type &&
                       intf.pipeline_stage == PipelineStage::Fragment) {
                if (intf.storage_class == StorageClass::Output) {
                    is_fragment_output_interface = true;
                } else {
                    is_fragment_input_interface = true;
                }
            }
        }

        if (is_vertex_input_interface) {
            binding_manager.decorate(spv, PipelineStage::Vertex, s, decl_ids[s.name.name], true);
        }
        if (is_vertex_output_interface) {
            binding_manager
                .decorate(spv, PipelineStage::Vertex, s, decl_ids[s.name.name], false);
        }
        if (is_fragment_input_interface && !is_vertex_output_interface) {
            binding_manager.decorate(spv, PipelineStage::Fragment, s, decl_ids[s.name.name], true);
        }
        if (is_fragment_output_interface) {
            binding_manager
                .decorate(spv, PipelineStage::Fragment, s, decl_ids[s.name.name], false);
        }
    }

    void generate_struct(const PoolStr& name, const TypeInfo::Struct& s) {
        std::vector<uint32_t> ops;

        for (const auto& member : s.members) {
            ops.push_back(resolve_type(member.type->name));
        }

        spv.TypeStruct(decl_ids[name], ops.data(), ops.size());
    }

    void generate_functions() {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                generate_function(decl->get<Decl::Function>());
            }
        }
    }

    void generate_function_debug_info(const Decl::Function& f) {
        spv.Name(decl_ids[f.name.name], f.name.name.c_str());
    }

    void generate_function_decorations(const Decl::Function& f) {}

    PoolStr clobber(const Decl::Function& f) {
        std::string name = "Fn(";
        bool first = true;
        for (const TypedIdentifier& param : f.params) {
            if (!first) {
                name += ",";
            }
            first = false;
            name += param.type.name.name.c_str();
        }

        name += ")->(";
        first = true;
        if (f.rets.size() == 0) {
            name += "void";
        } else {
            for (const TypedIdentifier& ret : f.rets) {
                if (!first) {
                    name += ",";
                }
                first = false;
                name += ret.type.name.name.c_str();
            }
        }
        name += ")";

        return arena.string_pool.add(name);
    }

    PoolStr clobber(const Decl::Struct& s) {
        std::string name = "Struct(";
        bool first = true;
        for (const TypedIdentifier& member : s.members) {
            if (!first) {
                name += ",";
            }
            first = false;
            name += member.type.name.name.c_str();
        }
        name += ")";

        return arena.string_pool.add(name);
    }

    PoolStr clobber(std::vector<PoolStr>& names) {
        std::string name = "Tuple(";
        bool first = true;
        for (const PoolStr& n : names) {
            if (!first) {
                name += ",";
            }
            first = false;
            name += n.c_str();
        }
        name += ")";

        return arena.string_pool.add(name);
    }

    void generate_function_types(Decl::Function& f) {
        // check if entry point
        // if entry point, it's a void() function
        bool is_entry_point = false;

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Pipeline>()) {
                Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                for (PipelineParameter& param : p.params) {
                    if (param.value.name == f.name.name) {
                        is_entry_point = true;
                    }
                }
            }
        }

        if (is_entry_point) {
            if (std::find_if(decl_ids.begin(), decl_ids.end(), [&](const auto& kvp) {
                    const PoolStr& t_name = std::get<0>(kvp);
                    return t_name == "Fn()->(void)";
                }) != decl_ids.end()) {
                f.return_type_id = decl_ids[arena.string_pool.add("void")];
                return;
            } else {
                uint32_t return_type_id = resolve_type(arena.string_pool.add("void"));

                uint32_t type_id = spv.TypeFunctionNew(return_type_id, nullptr, 0);
                f.return_type_id = return_type_id;

                decl_ids[arena.string_pool.add("Fn()->(void)")] = type_id;
                return;
            }
        }

        std::vector<uint32_t> ops;

        for (const TypedIdentifier& param : f.params) {
            ops.push_back(resolve_type(param.type.name.name));
        }

        std::vector<PoolStr> return_types;

        for (const TypedIdentifier& ret : f.rets) {
            return_types.push_back(ret.type.name.name);
        }

        uint32_t return_type;

        if (return_types.size() == 0) {
            return_type = resolve_type(arena.string_pool.add("void"));
        } else if (return_types.size() == 1) {
            return_type = resolve_type(return_types[0]);
        } else {
            std::vector<uint32_t> ops;
            for (const PoolStr& return_type : return_types) {
                ops.push_back(resolve_type(return_type));
            }

            return_type = spv.TypeStructNew(ops.data(), ops.size());

            decl_ids[clobber(return_types)] = return_type;
        }

        f.return_type_id = return_type;

        uint32_t type_id = spv.TypeFunctionNew(return_type, ops.data(), ops.size());

        decl_ids[clobber(f)] = type_id;
    }

    void generate_function(const Decl::Function& f) {
        bool is_vertex_entry_point = false;
        bool is_fragment_entry_point = false;

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Pipeline>()) {
                Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                for (PipelineParameter& param : p.params) {
                    if (param.name.name == "Vertex" && param.value.name == f.name.name) {
                        is_vertex_entry_point = true;
                    } else if (param.name.name == "Fragment" &&
                               param.value.name == f.name.name) {
                        is_fragment_entry_point = true;
                    }
                }
            }
        }

        bool is_entry_point = is_vertex_entry_point || is_fragment_entry_point;

        std::vector<uint32_t> ops;

        uint32_t type_id = is_entry_point ? decl_ids[arena.string_pool.add("Fn()->(void)")]
                                          : decl_ids[clobber(f)];

        uint32_t function_id = decl_ids[f.name.name];

        spv.Function(f.return_type_id, function_id, spv::FunctionControlMaskNone, type_id);

        Opt<VariableInstance> return_variable;

        open_scope();
        if (!is_entry_point) {
            for (const auto& param : f.params) {
                uint32_t param_id = spv.FunctionParameterNew((*param.type.resolved_type)->id);
                add_variable(param.name.name, param_id, *param.type.resolved_type, std::nullopt);
            }

            for (const auto& ret : f.rets) {
                uint32_t variable_id = spv.VariableNew(
                    (*ret.type.resolved_type)->get_pointer_type(spv::StorageClassFunction),
                    spv::StorageClassFunction
                );

                add_variable(ret.name.name, variable_id, *ret.type.resolved_type, spv::StorageClassFunction);
                return_variable = find_variable(ret.name.name);
            }
        } else {
            for (auto& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Vertex && is_vertex_entry_point) {
                    add_variable(gi.name, gi.id, gi.type, static_cast<spv::StorageClass>(gi.storage_class));
                } else if (gi.pipeline_stage == PipelineStage::Fragment && is_fragment_entry_point) {
                    add_variable(gi.name, gi.id, gi.type, static_cast<spv::StorageClass>(gi.storage_class));
                }
            }

            for (auto& builtin : builtins) {
                Opt<Ref<TypeInfo>> float4_type;

                for (Ref<TypeInfo> ti : arena.types) {
                    if (ti->id == builtin.second.type_id) {
                        float4_type = ti;
                    }
                }

                add_variable(
                    builtin.first,
                    builtin.second.id,
                    *float4_type,
                    builtin.second.storage_class
                );
            }
        }

        if (f.rets.size() > 0 && !is_entry_point) {
            BlockInfo block_info = generate_executable_block(f.stmts, return_variable);
        } else {
            generate_executable_block(f.stmts, std::nullopt);
        }

        close_scope();

        spv.FunctionEnd();
    }

    uint32_t resolve_type(const PoolStr& name) {
        auto it = decl_ids.find(name);
        if (it != decl_ids.end()) {
            return it->second;
        }
        return 0;
    }

    Decl::Function& find_function(const PoolStr& name) {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                Decl::Function& f = decl->get<Decl::Function>();
                if (f.name.name == name) {
                    return f;
                }
            }
        }
        assert(false);
    }

    void open_scope() {
        variable_scopes.push_back({});
    }

    void close_scope() {
        variable_scopes.pop_back();
    }

    Opt<VariableInstance> find_variable(const PoolStr& name) {
        for (int i = variable_scopes.size() - 1; i >= 0; i--) {
            auto it = variable_scopes[i].variables.find(name);
            if (it != variable_scopes[i].variables.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    void add_variable(const PoolStr& name, uint32_t id, Ref<TypeInfo> type, Opt<spv::StorageClass> storage_class) {
        variable_scopes.back().variables[name] = { id, type, storage_class };
    }
    struct BlockInfo {
        uint32_t statement_count = 0;
        uint32_t label_id = 0;
    };

    BlockInfo generate_executable_block(const std::vector<Ref<Stmt>>& stmts, Opt<VariableInstance> return_variable) {
        BlockInfo block_info;
        bool has_return = false;
        block_info.label_id = spv.LabelNew();
        open_scope();

        for (const Ref<Stmt>& stmt : stmts) {
            if (stmt->is<Stmt::Return>()) {
                has_return = true;
                if (return_variable) {
                    uint32_t return_value_id = return_variable->id;
                    uint32_t value = spv.LoadNew(
                        (*return_variable->type)->get_pointer_type(spv::StorageClassFunction),
                        return_variable->id
                    );
                    spv.ReturnValue(value);
                } else {
                    spv.Return();
                }
            } else if (stmt->is<Stmt::Var>()) {
                const Stmt::Var& var_stmt = stmt->get<Stmt::Var>();
                uint32_t var_id = spv.VariableNew(
                    (*var_stmt.typedIdentifier.type.resolved_type)->get_pointer_type(spv::StorageClassFunction),
                    spv::StorageClassFunction
                );
                if (var_stmt.expr) {
                    ExprResult expr_result = generate_expression(**var_stmt.expr, &**var_stmt.typedIdentifier.type.resolved_type);
                    spv.Store(var_id, expr_result.id);
                }

                add_variable(
                    var_stmt.typedIdentifier.name.name,
                    var_id,
                    *var_stmt.typedIdentifier.type.resolved_type,
                    spv::StorageClassFunction
                );
            } else if (stmt->is<Stmt::ExprStmt>()) {
                const Stmt::ExprStmt& expr_stmt = stmt->get<Stmt::ExprStmt>();
                ExprResult expr_result = generate_expression(*expr_stmt.expr, nullptr);
            }
        }

        if (!has_return) {
            spv.Return();
        }

        close_scope();

        return block_info;
    }

    struct ExprResult {
        uint32_t id;
        Opt<VariableInstance> variable;
        Ref<TypeInfo> type;
    };

    ExprResult generate_expression(const Expr& expr, const TypeInfo* expected_type) {
        return std::visit([this, expected_type](const auto& e) { return this->generate_expr(e, expected_type); },
            expr.data
        );
    }

    enum class OpFamily {
        Float,
        Int,
        Uint,
        Bool,
        VectorScalar,
        VectorMatrix,
        MatrixScalar,
        MatrixVector,
        MatrixMatrix,
        StructStruct,
    };

    OpFamily get_op_family(const TypeInfo& a, const TypeInfo& b) {
        if (a.is<TypeInfo::Struct>() && b.is<TypeInfo::Struct>()) {
            return OpFamily::StructStruct;
        }

        TypeInfo::Primitive a_primitive = a.get_underlying_primitive();
        TypeInfo::Primitive b_primitive = b.get_underlying_primitive();

        if (a.is<TypeInfo::Primitive>() && b.is<TypeInfo::Primitive>()) {
            switch (a_primitive.primitive) {
                case TypeInfo::BuiltinPrimitive::Float:
                    return OpFamily::Float;
                case TypeInfo::BuiltinPrimitive::Int:
                    return OpFamily::Int;
                case TypeInfo::BuiltinPrimitive::Uint:
                    return OpFamily::Uint;
                case TypeInfo::BuiltinPrimitive::Bool:
                    return OpFamily::Bool;
            }
        } else if (a.is<TypeInfo::Vector>() && b.is<TypeInfo::Vector>()) {
            switch (a_primitive.primitive) {
                case TypeInfo::BuiltinPrimitive::Float:
                    return OpFamily::Float;
                case TypeInfo::BuiltinPrimitive::Int:
                    return OpFamily::Int;
                case TypeInfo::BuiltinPrimitive::Uint:
                    return OpFamily::Uint;
                case TypeInfo::BuiltinPrimitive::Bool:
                    return OpFamily::Bool;
            }
        } else if (a.is<TypeInfo::Vector>() && b.is<TypeInfo::Primitive>()) {
            return OpFamily::VectorScalar;
        } else if (a.is<TypeInfo::Matrix>() && b.is<TypeInfo::Primitive>()) {
            return OpFamily::MatrixScalar;
        } else if (a.is<TypeInfo::Matrix>() && b.is<TypeInfo::Matrix>()) {
            return OpFamily::MatrixMatrix;
        } else if (a.is<TypeInfo::Vector>() && b.is<TypeInfo::Matrix>()) {
            return OpFamily::VectorMatrix;
        } else if (a.is<TypeInfo::Matrix>() && b.is<TypeInfo::Vector>()) {
            return OpFamily::MatrixVector;
        }
    }

    ExprResult generate_expr(const Expr::Binary& b, const TypeInfo* expected_type) {
        ExprResult left = this->generate_expression(*b.lhs, expected_type);
        ExprResult right = this->generate_expression(*b.rhs, &*left.type);

        OpFamily op_family = get_op_family(*left.type, *right.type);

        Opt<VariableInstance> out_var{};
        uint32_t res = spv.get_id();
        switch (b.op) {
            case Expr::BinaryOp::Add:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FAdd(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IAdd(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Sub:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FSub(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.ISub(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Mul:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FMul(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IMul(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::VectorScalar:
                        spv.VectorTimesScalar(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::MatrixScalar:
                        spv.MatrixTimesScalar(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::MatrixVector:
                        spv.MatrixTimesVector(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::VectorMatrix:
                        spv.VectorTimesMatrix(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::MatrixMatrix:
                        spv.MatrixTimesMatrix(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Div:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FDiv(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                        spv.SDiv(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.UDiv(left.type->id, res, left.id, right.id);
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Mod:
                switch (op_family) {
                    case OpFamily::Int:
                        spv.SMod(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.UMod(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Equal:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Bool:
                        spv.LogicalEqual(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::NotEqual:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdNotEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.INotEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Bool:
                        spv.LogicalNotEqual(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Less:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdLessThan(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                        spv.SLessThan(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.ULessThan(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::LessEqual:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdLessThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                        spv.SLessThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.ULessThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Greater:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdGreaterThan(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                        spv.SGreaterThan(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.UGreaterThan(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::GreaterEqual:
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FOrdGreaterThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Int:
                        spv.SGreaterThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    case OpFamily::Uint:
                        spv.UGreaterThanEqual(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;

            case Expr::BinaryOp::Or:
                switch (op_family) {
                    case OpFamily::Bool:
                        spv.LogicalOr(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::And:
                switch (op_family) {
                    case OpFamily::Bool:
                        spv.LogicalAnd(left.type->id, res, left.id, right.id);
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;

            case Expr::BinaryOp::Assign:
                spv.Store(left.variable->id, right.id);
                spv.Load(left.type->id, res, left.variable->id);
                out_var = left.variable;
                break;
            case Expr::BinaryOp::AddAssign: {
                uint32_t add_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.Load(left.type->id, add_res, left.variable->id);
                        spv.FAdd(left.type->id, res, add_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.Load(left.type->id, add_res, left.variable->id);
                        spv.IAdd(left.type->id, res, add_res, right.id);
                        out_var = left.variable;
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
            case Expr::BinaryOp::SubAssign: {
                uint32_t sub_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.Load(left.type->id, sub_res, left.variable->id);
                        spv.FSub(left.type->id, res, sub_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.Load(left.type->id, sub_res, left.variable->id);
                        spv.ISub(left.type->id, res, sub_res, right.id);
                        out_var = left.variable;
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
            case Expr::BinaryOp::MulAssign: {
                uint32_t mul_res = spv.get_id();
                spv.Load(left.type->id, mul_res, left.variable->id);
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FMul(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IMul(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::VectorScalar:
                        spv.VectorTimesScalar(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::MatrixScalar:
                        spv.MatrixTimesScalar(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::MatrixVector:
                        spv.MatrixTimesVector(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::VectorMatrix:
                        spv.VectorTimesMatrix(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::MatrixMatrix:
                        spv.MatrixTimesMatrix(left.type->id, res, mul_res, right.id);
                        out_var = left.variable;
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
            case Expr::BinaryOp::DivAssign: {
                uint32_t div_res = spv.get_id();
                spv.Load(left.type->id, div_res, left.variable->id);
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FDiv(left.type->id, res, div_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Int:
                        spv.SDiv(left.type->id, res, div_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Uint:
                        spv.UDiv(left.type->id, res, div_res, right.id);
                        out_var = left.variable;
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
            case Expr::BinaryOp::ModAssign: {
                uint32_t mod_res = spv.get_id();
                spv.Load(left.type->id, mod_res, left.variable->id);
                switch (op_family) {
                    case OpFamily::Int:
                        spv.SMod(left.type->id, res, mod_res, right.id);
                        out_var = left.variable;
                        break;
                    case OpFamily::Uint:
                        spv.UMod(left.type->id, res, mod_res, right.id);
                        out_var = left.variable;
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
        }

        return { res, out_var, left.type };
    }

    ExprResult generate_expr(const Expr::Unary& u, const TypeInfo* expected_type) {
        ExprResult inner = generate_expression(*u.expr, expected_type);
        TypeInfo::BuiltinPrimitive underlying_primitive =
            inner.type->get_underlying_primitive().primitive;
        uint32_t res = spv.get_id();
        switch (u.op) { 
            case Expr::UnaryOp::Neg:
                switch (underlying_primitive) {
                    case TypeInfo::BuiltinPrimitive::Float:
                        spv.FNegate(inner.type->id, res, inner.id);
                        break;
                    case TypeInfo::BuiltinPrimitive::Int:
                        spv.SNegate(inner.type->id, res, inner.id);
                        break;
                    default:
                        assert(false);
                }
                break;
            case Expr::UnaryOp::Not:
                switch (underlying_primitive) {
                    case TypeInfo::BuiltinPrimitive::Bool:
                        spv.LogicalNot(inner.type->id, res, inner.id);
                        break;
                    default:
                        assert(false);
                }
                break;
        }

        return { res, std::nullopt, inner.type };
    }

    ExprResult generate_expr(const Expr::Call& c, const TypeInfo* expected_type) {
        std::vector<uint32_t> ops;
        const Decl::Function& fun = find_function(c.name.name);
        for (int i = 0; i < fun.params.size(); i++) {
            const auto& arg = c.args[i];
            const auto& target = fun.params[i].type.resolved_type;
            ExprResult arg_result = generate_expression(*arg, &**target);
            ops.push_back(arg_result.id);
        }
        uint32_t res = spv.get_id();
        spv.FunctionCall(
            fun.return_type_id,
            res,
            decl_ids[fun.name.name],
            ops.data(),
            ops.size()
        );
        return { res, std::nullopt, *fun.rets[0].type.resolved_type };
    }

    ExprResult generate_expr(const Expr::ListAccess& la, const TypeInfo* expected_type) {
        ExprResult list = generate_expression(*la.list, nullptr);
        ExprResult index = generate_expression(*la.index, &**get_type_info("int"));
        uint32_t res = spv.get_id();
        spv.AccessChain(list.type->id, res, list.id, &index.id, 1);
        assert(list.type->is<TypeInfo::Array>());
        TypeInfo::Array& array = list.type->get<TypeInfo::Array>();
        return { res, std::nullopt, array.element };
    }

    ExprResult generate_expr(const Expr::FieldAccess& fa, const TypeInfo* expected_type) {
        ExprResult base = generate_expression(*fa.object, nullptr);
        uint32_t ptr_res = spv.get_id();

        uint32_t res = spv.get_id();

        assert(base.type->is<TypeInfo::Struct>());

        const TypeInfo::Struct& s = base.type->get<TypeInfo::Struct>();
        VariableInstance virtual_field_variable;
        for (int i = 0; i < s.members.size(); i++) {
            if (s.members[i].name == fa.field.name) {
                uint32_t constant_id = get_constant_int(i);
                spv.AccessChain(
                    s.members[i].type->get_pointer_type(*base.variable->storage_class),
                    ptr_res,
                    base.variable->id,
                    &constant_id,
                    1
                );
                virtual_field_variable = { ptr_res, s.members[i].type, base.variable->storage_class };
                break;
            }
        }

        spv.Load((*virtual_field_variable.type)->id, res, ptr_res);

        return { res, virtual_field_variable, *virtual_field_variable.type };
    }

    ExprResult generate_expr(const Expr::NumberLiteral& nl, const TypeInfo* expected_type) {
        double v = nl.value;
        bool could_be_int = v == static_cast<int>(v);
        bool could_be_uint = v == static_cast<uint32_t>(v);

        if (expected_type) {
        } else {
            return { get_constant_float(v), std::nullopt, *get_type_info("float") };
        }
    }

    ExprResult generate_expr(const Expr::VariableAccess& va, const TypeInfo* expected_type) {
        VariableInstance var = *find_variable(va.name.name);
        if (!var.storage_class.has_value()) {
            return { var.id, std::nullopt, *var.type };
        } else {
            uint32_t id = spv.get_id();
            spv.Load((*var.type)->id, id, var.id);
            return { id, var, *var.type };
        }
    }

    void flush(std::ostream& out) {
        for (unsigned i = 0; i < spv.words.size(); i++) {
            out.write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
        }
    }
};
