#pragma once

#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include "spirv_binary_container.hpp"

#include "repr.hpp"
#include "arena.hpp"

#include <algorithm>
#include <ostream>
#include <unordered_map>

class CodeGenerator final {
  public:
    CompilationArena& arena;
    spv_binary::BinaryContainer spv;

    uint32_t glsl_ext;
    std::unordered_map<PoolStr, uint32_t> decl_ids;

    CodeGenerator(CompilationArena& arena) : arena(arena) {}

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

        generate_entry_points();
        generate_debug_info();
        generate_decorations();
        generate_builtins();
        generate_types();
        generate_constants();
        generate_functions();

        spv.update_bound();
    }

    void generate_prelude() {
        spv.Capability(spv::CapabilityShader);
        glsl_ext = spv.ExtInstImportNew("GLSL.std.450");
        spv.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
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
                    } else
                    if (param.name.name == "Fragment") {
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

        for (PoolStr& name : fragment_entry_points) {
            Decl::Function& f = find_function(name);

            std::vector<uint32_t> ops;
            for (TypedIdentifier& param : f.params) {
                ops.push_back(resolve_type(param.type.name));
            }

            spv.EntryPoint(
                spv::ExecutionModelFragment,
                decl_ids[name],
                name.c_str(),
                nullptr,
                0
            );
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
        uint32_t void_id = decl_ids[arena.string_pool.add("void")] = spv.TypeVoidNew();
        uint32_t bool_id = decl_ids[arena.string_pool.add("bool")] = spv.TypeBoolNew();
        uint32_t float_id = decl_ids[arena.string_pool.add("float")] = spv.TypeFloatNew(32);
        uint32_t uint_id = decl_ids[arena.string_pool.add("uint")] = spv.TypeIntNew(32, 0);
        uint32_t int_id = decl_ids[arena.string_pool.add("int")] = spv.TypeIntNew(32, 1);

        decl_ids[arena.string_pool.add("float2")] = spv.TypeVectorNew(float_id, 2);
        decl_ids[arena.string_pool.add("float3")] = spv.TypeVectorNew(float_id, 3);
        decl_ids[arena.string_pool.add("float4")] = spv.TypeVectorNew(float_id, 4);

        decl_ids[arena.string_pool.add("uint2")] = spv.TypeVectorNew(uint_id, 2);
        decl_ids[arena.string_pool.add("uint3")] = spv.TypeVectorNew(uint_id, 3);
        decl_ids[arena.string_pool.add("uint4")] = spv.TypeVectorNew(uint_id, 4);

        decl_ids[arena.string_pool.add("int2")] = spv.TypeVectorNew(int_id, 2);
        decl_ids[arena.string_pool.add("int3")] = spv.TypeVectorNew(int_id, 3);
        decl_ids[arena.string_pool.add("int4")] = spv.TypeVectorNew(int_id, 4);
    }

    void generate_types() {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                generate_struct(decl->get<Decl::Struct>());
            }
        }

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                generate_function_types(decl->get<Decl::Function>());
            }
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
            spv.MemberDecorate(
                decl_ids[s.name.name],
                n_ops,
                spv::DecorationOffset,
                &offset,
                1
            );

            offset += get_type_size_offset(offset, member.type);
            n_ops++;
        }
    }

    void generate_struct(const Decl::Struct& s) {
        std::vector<uint32_t> ops;

        for (const TypedIdentifier& member : s.members) {
            ops.push_back(resolve_type(member.type.name));
        }

        spv.TypeStruct(decl_ids[s.name.name], ops.data(), ops.size());
    }

    void generate_constants() {}

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
            name += param.type.name.c_str();
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
                name += ret.type.name.c_str();
            }
        }
        name += ")";

        return arena.string_pool.add(name);
    }

    void generate_function_types(const Decl::Function& f) {
        std::vector<uint32_t> ops;

        for (const TypedIdentifier& param : f.params) {
            ops.push_back(resolve_type(param.type.name));
        }

        std::vector<PoolStr> return_types;

        for (const TypedIdentifier& ret : f.rets) {
            return_types.push_back(ret.type.name);
        }

        uint32_t return_type;

        if (return_types.size() == 0) {
            return_type = resolve_type(arena.string_pool.add("void"));
        }
        else if (return_types.size() == 1) {
            return_type = resolve_type(return_types[0]);
        }
        else {
            // TODO: handle multiple return types
            for (PoolStr& return_type : return_types) {
                printf("RETURN TYPE: %s\n", return_type.c_str());
            }
            assert(false);
        }

        uint32_t type_id = spv.TypeFunctionNew(return_type, ops.data(), ops.size());

        decl_ids[clobber(f)] = type_id;
    }

    void generate_function(const Decl::Function& f) {
        std::vector<uint32_t> ops;

        uint32_t type_id = decl_ids[clobber(f)];

        uint32_t function_id = decl_ids[f.name.name];

        spv.Function(
            decl_ids[arena.string_pool.add("void")],
            function_id,
            spv::FunctionControlMaskNone,
            type_id
        );

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
