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

    void decorate(spv_binary::BinaryContainer& spv, PipelineStage context, const Decl::Struct& s, uint32_t struct_id) {
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

    void decorate_as_uniform(spv_binary::BinaryContainer& spv, const Decl::Struct& s, uint32_t struct_id) {
    }

    void allocate_variable(
        spv_binary::BinaryContainer& spv, GlobalInterface& gi, uint32_t type_id
    ) {
        uint32_t pointer_type = spv.TypePointerNew((uint32_t)gi.storage_class, type_id);
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

class CodeGenerator final {
  public:
    CompilationArena& arena;
    spv_binary::BinaryContainer spv;

    uint32_t glsl_ext;
    std::unordered_map<PoolStr, uint32_t> decl_ids;
    std::unordered_map<int32_t, uint32_t> int_constant_ids;

    std::vector<GlobalInterface> global_interfaces;

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
        generate_entry_points();
        generate_debug_info();
        generate_decorations();
        generate_builtins();
        generate_types();
        generate_functions();

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

        spv.update_bound();
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

        std::vector<Ref<TypeInfo>> vertex_inputs;
        std::vector<Ref<TypeInfo>> vertex_outputs;
        std::vector<Ref<TypeInfo>> fragment_inputs;
        std::vector<Ref<TypeInfo>> fragment_outputs;

        for (PoolStr& name : vertex_entry_points) {
            Decl::Function& f = find_function(name);

            for (TypedIdentifier& param : f.params) {
                vertex_inputs.push_back(param.type.resolved_type.value());
            }

            for (TypedIdentifier& ret : f.rets) {
                vertex_outputs.push_back(ret.type.resolved_type.value());
            }
        }

        for (PoolStr& name : fragment_entry_points) {
            Decl::Function& f = find_function(name);

            for (TypedIdentifier& param : f.params) {
                fragment_inputs.push_back(param.type.resolved_type.value());
            }

            for (TypedIdentifier& ret : f.rets) {
                fragment_outputs.push_back(ret.type.resolved_type.value());
            }
        }

        for (Ref<TypeInfo>& type : vertex_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                { binding_manager.get_input_storage_class(PipelineStage::Vertex), PipelineStage::Vertex, type, id, 0 }
            );
        }

        for (Ref<TypeInfo>& type : vertex_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({ StorageClass::Output, PipelineStage::Vertex, type, id, 0 });
        }

        for (Ref<TypeInfo>& type : fragment_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                { binding_manager.get_input_storage_class(PipelineStage::Fragment), PipelineStage::Fragment, type, id, 0 }
            );
        }

        for (Ref<TypeInfo>& type : fragment_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back({ StorageClass::Output, PipelineStage::Fragment, type, id, 0 });
        }
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
                if ((gi.storage_class == StorageClass::Input ||
                     gi.storage_class == StorageClass::Output) &&
                    gi.pipeline_stage == PipelineStage::Vertex) {
                    interfaces.push_back(gi.id);
                }
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
                if ((gi.storage_class == StorageClass::Input ||
                     gi.storage_class == StorageClass::Output) &&
                    gi.pipeline_stage == PipelineStage::Fragment) {
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
    }

    void generate_builtins() {
        decl_ids[arena.string_pool.add("void")] = spv.TypeVoidNew();
        decl_ids[arena.string_pool.add("bool")] = spv.TypeBoolNew();
        decl_ids[arena.string_pool.add("int")] = spv.TypeIntNew(32, 1);
        decl_ids[arena.string_pool.add("uint")] = spv.TypeIntNew(32, 0);
        decl_ids[arena.string_pool.add("float")] = spv.TypeFloatNew(32);
    }

    uint32_t get_constant_int(int32_t value) {
        auto it = int_constant_ids.find(value);
        if (it != int_constant_ids.end()) {
            return it->second;
        }

        uint32_t id = spv.ConstantNew(decl_ids[arena.string_pool.add("int")], value);
        int_constant_ids[value] = id;
        return id;
    }

    void generate_type_from_info(const TypeInfo& type_info) {
        std::visit(
            overloaded{
                [&](const TypeInfo::Primitive&) {},
                [&](const TypeInfo::Vector& v) {
                    uint32_t underlying_type_id = resolve_type(v.element->name);
                    uint32_t type_id = spv.TypeVectorNew(underlying_type_id, v.size);
                    decl_ids[type_info.name] = type_id;
                },
                [&](const TypeInfo::Matrix& m) {
                    uint32_t underlying_type_id = resolve_type(m.vector_element->name);
                    uint32_t type_id = spv.TypeMatrixNew(underlying_type_id, m.columns);
                    decl_ids[type_info.name] = type_id;
                },
                [&](const TypeInfo::Struct& s) {
                    generate_struct(type_info.name, s);
                },
                [&](const TypeInfo::Array& a) {
                    uint32_t underlying_type_id = resolve_type(a.element->name);
                    if (a.is_sized) {
                        uint32_t type_id =
                            spv.TypeArrayNew(underlying_type_id, get_constant_int(a.size));
                        decl_ids[type_info.name] = type_id;
                    } else {
                        uint32_t type_id = spv.TypeRuntimeArrayNew(underlying_type_id);
                        decl_ids[type_info.name] = type_id;
                    }
                },
            },
            type_info.data
        );
    }

    void generate_types() {
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

        bool is_fragment_interface = false;
        bool is_vertex_interface = false;

        for (const auto& intf : global_interfaces) {
            if (intf.type == s.resolved_type && intf.pipeline_stage == PipelineStage::Vertex) {
                is_vertex_interface = true;
            } else if (intf.type == s.resolved_type &&
                       intf.pipeline_stage == PipelineStage::Fragment) {
                is_fragment_interface = true;
            }
        }

        if (is_vertex_interface) {
            binding_manager.decorate(spv, PipelineStage::Vertex, s, decl_ids[s.name.name]);
        }
        if (is_fragment_interface) {
            binding_manager.decorate(spv, PipelineStage::Fragment, s, decl_ids[s.name.name]);
        }
    }

    void generate_struct(const PoolStr& name, const TypeInfo::Struct& s) {
        std::vector<uint32_t> ops;

        for (const Ref<TypeInfo>& member : s.members) {
            ops.push_back(resolve_type(member->name));
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

        std::vector<uint32_t> ops;

        uint32_t type_id = is_entry_point ? decl_ids[arena.string_pool.add("Fn()->(void)")]
                                          : decl_ids[clobber(f)];

        uint32_t function_id = decl_ids[f.name.name];

        spv.Function(f.return_type_id, function_id, spv::FunctionControlMaskNone, type_id);

        spv.LabelNew();
        spv.Return();

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

    void flush(std::ostream& out) {
        for (unsigned i = 0; i < spv.words.size(); i++) {
            out.write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
        }
    }
};
