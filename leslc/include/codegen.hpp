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
        spv.Source(spv::SourceLanguageUnknown, 0);
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
    }

    void generate_decorations() {}

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
    }

    void generate_struct(const Decl::Struct& s) {
        std::vector<uint32_t> ops;

        uint32_t offset = 0;

        for (const TypedIdentifier& member : s.members) {
            ops.push_back(resolve_type(member.type.name));
            spv.MemberDecorate(
                decl_ids[s.name.name],
                ops.size() - 1,
                spv::DecorationOffset,
                &offset,
                1
            );
            spv.MemberName(decl_ids[s.name.name], ops.size() - 1, member.name.name.c_str());

            offset += 4;
        }

        spv.TypeStruct(decl_ids[s.name.name], ops.data(), ops.size());
        spv.Name(decl_ids[s.name.name], s.name.name.c_str());
    }

    void generate_constants() {}

    void generate_functions() {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                generate_function(decl->get<Decl::Function>());
            }
        }
    }

    void generate_function(const Decl::Function& f) {}

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
