#pragma once

#include "lesl/ref_container.hpp"
#include "spirv/1.0/spirv.hpp"

#include "spirv_binary_container.hpp"

#include "lesl/repr.hpp"
#include "lesl/arena.hpp"
#include "lesl/stringpool.hpp"

#include "lesl/builtin_functions.hpp"
#include "lesl/binding_manager.hpp"
#include "lesl/codegen_helpers.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <ostream>
#include <set>
#include <unordered_map>
#include <variant>

namespace lesl {
class CodeGenerator final {
  public:
    CompilationArena& arena;
    spvbc::BinaryContainer spv;

    Opt<std::string> selected_pipeline_name;
    Opt<Ref<Decl>> selected_pipeline;

    uint32_t constants_insert_point;
    uint32_t glsl_ext;
    std::unordered_map<PoolStr, uint32_t> decl_ids;
    std::unordered_map<PoolStr, BuiltinInfo> builtins;
    std::unordered_map<int32_t, uint32_t> int_constant_ids;
    std::unordered_map<uint32_t, uint32_t> uint_constant_ids;
    std::unordered_map<float, uint32_t> float_constant_ids;
    std::vector<uint32_t> constant_block;

    std::vector<GlobalInterface> global_interfaces;

    VariableScopeTree variable_scope_tree;
    std::vector<int32_t> current_variable_scope_path;

    BindingManagerInterface& binding_manager;

    RefContainer<ExprResult> expr_gen_results;

    CodeGenerator(
        CompilationArena& arena,
        BindingManagerInterface& binding_manager,
        Opt<std::string> pipeline
    )
        : arena(arena), selected_pipeline_name(pipeline), binding_manager(binding_manager) {
        if (pipeline) {
            for (Ref<Decl> decl : arena.decls) {
                if (decl->is<Decl::Pipeline>()) {
                    Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                    if (p.name.name == selected_pipeline_name.value()) {
                        selected_pipeline = decl;
                    }
                }
            }
        }
    }

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
    }

    void generate_prelude() {
        spv.Capability(spv::CapabilityShader);
        glsl_ext = spv.ExtInstImportNew("GLSL.std.450");
        spv.MemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    }

    void preallocate_interface_ids() {
        PoolStr fragment_entry_point;
        PoolStr vertex_entry_point;

        if (selected_pipeline) {
            Decl::Pipeline& p = selected_pipeline.value()->get<Decl::Pipeline>();
            for (PipelineParameter& param : p.params) {
                if (param.name.name == "Vertex") {
                    vertex_entry_point = param.value.name;
                } else if (param.name.name == "Fragment") {
                    fragment_entry_point = param.value.name;
                }
            }
        }

        std::vector<TypedIdentifier> vertex_inputs;
        std::vector<TypedIdentifier> vertex_outputs;
        std::vector<TypedIdentifier> fragment_inputs;
        std::vector<TypedIdentifier> fragment_outputs;

        Decl::Function& vf = find_function(vertex_entry_point);

        for (TypedIdentifier& param : vf.params) {
            vertex_inputs.push_back(param);
        }

        vertex_outputs.push_back(vf.ret);

        Decl::Function& ff = find_function(fragment_entry_point);

        for (TypedIdentifier& param : ff.params) {
            fragment_inputs.push_back(param);
        }

        fragment_outputs.push_back(ff.ret);

        for (TypedIdentifier& type : vertex_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                {
                    binding_manager.get_input_storage_class(
                        **type.type.resolved_type,
                        PipelineStage::Vertex
                    ),
                    PipelineStage::Vertex,
                    type.type.resolved_type.value(),
                    type.name.name,
                    id,
                    0,
                }
            );
        }

        for (TypedIdentifier& type : vertex_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                {
                    StorageClass::Output,
                    PipelineStage::Vertex,
                    type.type.resolved_type.value(),
                    type.name.name,
                    id,
                    0,
                }
            );
        }

        for (TypedIdentifier& type : fragment_inputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                {
                    binding_manager.get_input_storage_class(
                        **type.type.resolved_type,
                        PipelineStage::Fragment
                    ),
                    PipelineStage::Fragment,
                    type.type.resolved_type.value(),
                    type.name.name,
                    id,
                    0,
                }
            );
        }

        for (TypedIdentifier& type : fragment_outputs) {
            uint32_t id = spv.get_id();
            global_interfaces.push_back(
                {
                    StorageClass::Output,
                    PipelineStage::Fragment,
                    type.type.resolved_type.value(),
                    type.name.name,
                    id,
                    0,
                }
            );
        }
    }

    void preallocate_builtins() {
        uint32_t id = spv.get_id();
        builtins[arena.string_pool.add("POSITION")] = {
            id,
            0,
            0,
            spv::StorageClassOutput,
        };
    }

    void generate_entry_points() {
        std::set<PoolStr> fragment_entry_points;
        std::set<PoolStr> vertex_entry_points;

        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Pipeline>()) {
                Decl::Pipeline& p = decl->get<Decl::Pipeline>();
                if (selected_pipeline_name && p.name.name != selected_pipeline_name.value()) {
                    continue;
                }
                for (PipelineParameter& param : p.params) {
                    if (param.name.name == "Vertex") {
                        if (std::find(
                                vertex_entry_points.begin(),
                                vertex_entry_points.end(),
                                param.value.name
                            ) == vertex_entry_points.end()) {
                            vertex_entry_points.insert(param.value.name);
                        }
                    } else if (param.name.name == "Fragment") {
                        if (std::find(
                                fragment_entry_points.begin(),
                                fragment_entry_points.end(),
                                param.value.name
                            ) == fragment_entry_points.end()) {
                            fragment_entry_points.insert(param.value.name);
                        }
                    }
                }
            }
        }

        for (const PoolStr& name : vertex_entry_points) {
            Decl::Function& f = find_function(name);

            std::vector<uint32_t> ops;
            for (TypedIdentifier& param : f.params) {
                ops.push_back(resolve_type(param.type.name.name));
            }

            std::vector<uint32_t> interfaces;

            for (GlobalInterface& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Vertex &&
                    (gi.storage_class == StorageClass::Input ||
                     gi.storage_class == StorageClass::Output)) {
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

        for (const PoolStr& name : fragment_entry_points) {
            Decl::Function& f = find_function(name);

            std::vector<uint32_t> ops;
            for (TypedIdentifier& param : f.params) {
                ops.push_back(resolve_type(param.type.name.name));
            }

            std::vector<uint32_t> interfaces;

            for (GlobalInterface& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Fragment &&
                    (gi.storage_class == StorageClass::Input ||
                     gi.storage_class == StorageClass::Output)) {
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

        binding_manager.decorate_interfaces(spv, global_interfaces);

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
        spv.Variable(float4_output_ptr, position.id, spv::StorageClassOutput);
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
        constant_encode(
            decl_ids[arena.string_pool.add("int")],
            id,
            std::bit_cast<uint32_t>(value)
        );
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
        constant_encode(
            decl_ids[arena.string_pool.add("float")],
            id,
            std::bit_cast<uint32_t>(value)
        );
        return id;
    }

    void backfill_constants() {
        spv.insert(constant_block, constants_insert_point);
    }

    uint32_t
    try_add_storage_class_pointer(TypeInfo& type_info, spv::StorageClass storage_class) {
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
                [&](const TypeInfo::ImageSampler&) {
                    uint32_t type_id = spv.TypeImageNew(
                        decl_ids[arena.string_pool.add("float")],
                        spv::Dim2D,
                        0,
                        0,
                        0,
                        1,
                        spv::ImageFormatUnknown
                    );

                    uint32_t sampled_type_id = spv.TypeSampledImageNew(type_id);

                    decl_ids[type_info.name] = sampled_type_id;

                    type_info.id = decl_ids[type_info.name];

                    try_add_storage_class_pointer(type_info, spv::StorageClassUniformConstant);
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

        binding_manager.allocate_interface_variables(spv, global_interfaces);
    }

    void generate_struct_debug_info(const Decl::Struct& s) {
        int32_t n_ops = 0;

        for (const Decl::StructMember& member : s.members) {
            spv.MemberName(decl_ids[s.name.name], n_ops, member.name.name.c_str());

            n_ops++;
        }

        spv.Name(decl_ids[s.name.name], s.name.name.c_str());
    }

    uint32_t get_type_size_offset(uint32_t, const TypeInfo& type_info) {
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

        for (const Decl::StructMember& member : s.members) {
            spv.MemberDecorate(decl_ids[s.name.name], n_ops, spv::DecorationOffset, &offset, 1);
            if (member.interpolation != TypeInfo::InterpolationQualifier::None) {
                spv::Decoration decor = spv::DecorationFlat;
                switch (member.interpolation) {
                    case TypeInfo::InterpolationQualifier::None:
                        break;
                    case TypeInfo::InterpolationQualifier::Flat:
                        decor = spv::DecorationFlat;
                        break;
                    case TypeInfo::InterpolationQualifier::NoPerspective:
                        decor = spv::DecorationNoPerspective;
                        break;
                    case TypeInfo::InterpolationQualifier::Centroid:
                        decor = spv::DecorationCentroid;
                        break;
                }

                spv.MemberDecorate(decl_ids[s.name.name], n_ops, decor, nullptr, 0);
            }

            const TypeInfo& member_type_info = **member.type.resolved_type;

            if (member_type_info.is<TypeInfo::Matrix>()) {
                spv.MemberDecorate(
                    decl_ids[s.name.name],
                    n_ops,
                    spv::DecorationColMajor,
                    nullptr,
                    0
                );

                uint32_t stride = 16; // required for Vulkan uniform buffer layout

                spv.MemberDecorate(
                    decl_ids[s.name.name],
                    n_ops,
                    spv::DecorationMatrixStride,
                    &stride,
                    1
                );
            }

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
            } else if (
                intf.type == s.resolved_type && intf.pipeline_stage == PipelineStage::Fragment
            ) {
                if (intf.storage_class == StorageClass::Output) {
                    is_fragment_output_interface = true;
                } else {
                    is_fragment_input_interface = true;
                }
            }
        }

        if (is_vertex_input_interface) {
            binding_manager
                .decorate_struct(spv, PipelineStage::Vertex, s, decl_ids[s.name.name], true);
        }
        if (is_vertex_output_interface) {
            binding_manager
                .decorate_struct(spv, PipelineStage::Vertex, s, decl_ids[s.name.name], false);
        }
        if (is_fragment_input_interface && !is_vertex_output_interface) {
            binding_manager
                .decorate_struct(spv, PipelineStage::Fragment, s, decl_ids[s.name.name], true);
        }
        if (is_fragment_output_interface) {
            binding_manager
                .decorate_struct(spv, PipelineStage::Fragment, s, decl_ids[s.name.name], false);
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

    void generate_function_decorations(const Decl::Function&) {}

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
        name += f.ret.type.name.name.c_str();
        name += ")";

        return arena.string_pool.add(name);
    }

    PoolStr clobber(const Decl::Struct& s) {
        std::string name = "Struct(";
        bool first = true;
        for (const Decl::StructMember& member : s.members) {
            if (!first) {
                name += ",";
            }
            first = false;
            if (member.interpolation == TypeInfo::InterpolationQualifier::Flat) {
                name += "flat:";
            } else if (
                member.interpolation == TypeInfo::InterpolationQualifier::NoPerspective
            ) {
                name += "noperspective:";
            } else if (member.interpolation == TypeInfo::InterpolationQualifier::Centroid) {
                name += "centroid:";
            }
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

        uint32_t return_type = resolve_type(f.ret.type.name.name);

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
                    } else if (
                        param.name.name == "Fragment" && param.value.name == f.name.name
                    ) {
                        is_fragment_entry_point = true;
                    }
                }
            }
        }

        bool is_entry_point = is_vertex_entry_point || is_fragment_entry_point;

        // if it's an entry point, check if it's a part of the currently selected
        // pipeline
        if (selected_pipeline && is_entry_point) {
            bool found = false;
            Decl::Pipeline& p = selected_pipeline.value()->get<Decl::Pipeline>();
            if (p.name.name == selected_pipeline_name.value()) {
                for (PipelineParameter& param : p.params) {
                    if ((param.name.name == "Vertex" || param.name.name == "Fragment") &&
                        param.value.name == f.name.name) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                return; // skip functions that are not part of the current pipeline
            }
        }

        std::vector<uint32_t> ops;

        uint32_t type_id = is_entry_point ? decl_ids[arena.string_pool.add("Fn()->(void)")]
                                          : decl_ids[clobber(f)];

        uint32_t function_id = decl_ids[f.name.name];

        spv.Function(f.return_type_id, function_id, spv::FunctionControlMaskNone, type_id);

        Opt<VariableInstance> return_variable;

        Opt<uint32_t> label_id;

        variable_scope_tree.clear();
        current_variable_scope_path.clear();

        Ref<VariableScopeNode> function_scope_node = variable_scope_tree.root;

        if (!is_entry_point) {
            for (const auto& param : f.params) {
                uint32_t param_id = spv.FunctionParameterNew((*param.type.resolved_type)->id);
                add_variable(
                    param.name.name,
                    param_id,
                    *param.type.resolved_type,
                    std::nullopt
                );
            }

            label_id = spv.LabelNew();

            uint32_t variable_id = spv.VariableNew(
                (*f.ret.type.resolved_type)->get_pointer_type(spv::StorageClassFunction),
                spv::StorageClassFunction
            );

            add_variable(
                f.ret.name.name,
                variable_id,
                *f.ret.type.resolved_type,
                spv::StorageClassFunction
            );
            return_variable = find_variable(f.ret.name.name);
        } else {
            for (auto& gi : global_interfaces) {
                if (gi.pipeline_stage == PipelineStage::Vertex && is_vertex_entry_point) {
                    add_variable(
                        gi.name,
                        gi.id,
                        gi.type,
                        static_cast<spv::StorageClass>(gi.storage_class)
                    );
                } else if (
                    gi.pipeline_stage == PipelineStage::Fragment && is_fragment_entry_point
                ) {
                    add_variable(
                        gi.name,
                        gi.id,
                        gi.type,
                        static_cast<spv::StorageClass>(gi.storage_class)
                    );
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

            label_id = spv.LabelNew();
        }

        preallocate_function_variables_recursive(function_scope_node, f.stmts);

        bool returns_void = (*f.ret.type.resolved_type)->get_underlying_primitive().primitive ==
                            TypeInfo::BuiltinPrimitive::Void;

        if (!returns_void && !is_entry_point) {
            generate_executable_block(f.stmts, return_variable, label_id, true);
        } else {
            generate_executable_block(f.stmts, std::nullopt, label_id, true);
        }

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

    Opt<Ref<Decl>> try_find_function(const PoolStr& name) {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                Decl::Function& f = decl->get<Decl::Function>();
                if (f.name.name == name) {
                    return decl;
                }
            }
        }
        return std::nullopt;
    }

    void open_scope() {
        current_variable_scope_path.push_back(0);
    }

    void advance_scope() {
        current_variable_scope_path.back()++;
    }

    uint32_t close_scope() {
        uint32_t scope_index = current_variable_scope_path.back();
        current_variable_scope_path.pop_back();
        return scope_index;
    }

    void reopen_scope(uint32_t index) {
        current_variable_scope_path.push_back(index);
    }

    Opt<VariableInstance> find_variable(const PoolStr& name) {
        std::vector<int32_t> path = current_variable_scope_path;

        Ref<VariableScopeNode> node = variable_scope_tree.get_at(path).value();
        while (true) {
            auto it = node->variables.find(name);
            if (it != node->variables.end()) {
                return it->second;
            }

            if (path.size() == 0) {
                break;
            }

            path.pop_back();
            Opt<Ref<VariableScopeNode>> parent_node = node->parent;

            if (!parent_node.has_value()) {
                break;
            }

            node = parent_node.value();
        }
        return std::nullopt;
    }

    void add_variable(
        const PoolStr& name,
        uint32_t id,
        Ref<TypeInfo> type,
        Opt<spv::StorageClass> storage_class
    ) {
        Ref<VariableScopeNode> node =
            variable_scope_tree.get_at(current_variable_scope_path).value();

        node->variables[name] = { id, type, storage_class };
    }

    void preallocate_function_variables_recursive(
        Ref<VariableScopeNode> current_node,
        const std::vector<Ref<Stmt>>& stmts
    ) {
        for (const Ref<Stmt>& stmt : stmts) {
            if (stmt->is<Stmt::Var>()) {
                const Stmt::Var& var_stmt = stmt->get<Stmt::Var>();
                uint32_t var_id = spv.VariableNew(
                    (*var_stmt.typedIdentifier.type.resolved_type)
                        ->get_pointer_type(spv::StorageClassFunction),
                    spv::StorageClassFunction
                );

                current_node->variables[var_stmt.typedIdentifier.name.name] = {
                    var_id,
                    *var_stmt.typedIdentifier.type.resolved_type,
                    spv::StorageClassFunction
                };
            } else if (stmt->is<Stmt::IfStmt>()) {
                const Stmt::IfStmt& if_stmt = stmt->get<Stmt::IfStmt>();

                Ref<VariableScopeNode> then_node =
                    variable_scope_tree.create_node(current_node);

                preallocate_function_variables_recursive(then_node, if_stmt.then_branch);

                // Create a new child scope for the 'else' branch if it exists
                if (if_stmt.else_branch) {
                    Ref<VariableScopeNode> else_node =
                        variable_scope_tree.create_node(current_node);
                    preallocate_function_variables_recursive(else_node, *if_stmt.else_branch);
                }
            } else if (stmt->is<Stmt::For>()) {
                const Stmt::For& for_stmt = stmt->get<Stmt::For>();

                LoopScopeInfo loop_info{ spv.get_id(), spv.get_id() };

                Ref<VariableScopeNode> loop_node =
                    variable_scope_tree.create_node(current_node, loop_info);

                // Preallocate loop variable
                uint32_t var_id = spv.VariableNew(
                    (get_type_info("int").value())->get_pointer_type(spv::StorageClassFunction),
                    spv::StorageClassFunction
                );

                loop_node->variables[for_stmt.iterator_name] = { var_id,
                                                                 get_type_info("int").value(),
                                                                 spv::StorageClassFunction };

                preallocate_function_variables_recursive(loop_node, for_stmt.body);
            }
        }
    }

    struct BlockInfo {
        uint32_t statement_count = 0;
        uint32_t label_id = 0;
        bool exclude_ending_branch = false;
    };

    BlockInfo generate_executable_block(
        const std::vector<Ref<Stmt>>& stmts,
        Opt<VariableInstance> return_variable,
        Opt<uint32_t> known_label_id,
        bool is_root = false
    ) {
        BlockInfo block_info;
        bool has_return = false;
        if (known_label_id.has_value()) {
            block_info.label_id = known_label_id.value();
        } else {
            block_info.label_id = spv.LabelNew();
        }

        uint32_t subscope_index = 0;

        for (const Ref<Stmt>& stmt : stmts) {
            if (stmt->is<Stmt::Return>()) {
                has_return = true;
                if (return_variable) {
                    uint32_t value = spv.LoadNew(
                        (*return_variable->type)->get_pointer_type(spv::StorageClassFunction),
                        return_variable->id
                    );
                    spv.ReturnValue(value);
                } else {
                    spv.Return();
                }
            } else if (stmt->is<Stmt::Discard>()) {
                block_info.exclude_ending_branch = true;
                spv.Kill();
            } else if (stmt->is<Stmt::Var>()) {
                const Stmt::Var& var_stmt = stmt->get<Stmt::Var>();
                uint32_t var_id = find_variable(var_stmt.typedIdentifier.name.name).value().id;
                if (var_stmt.expr) {
                    Ref<ExprResult> expr_result = generate_expression(
                        **var_stmt.expr,
                        &**var_stmt.typedIdentifier.type.resolved_type
                    );
                    spv.Store(
                        var_id,
                        expr_result->load(spv, var_stmt.typedIdentifier.type.resolved_type)
                    );
                }
            } else if (stmt->is<Stmt::ExprStmt>()) {
                const Stmt::ExprStmt& expr_stmt = stmt->get<Stmt::ExprStmt>();
                Ref<ExprResult> _ = generate_expression(*expr_stmt.expr, nullptr);
            } else if (stmt->is<Stmt::IfStmt>()) {
                const Stmt::IfStmt& if_stmt = stmt->get<Stmt::IfStmt>();
                Ref<ExprResult> condition =
                    generate_expression(*if_stmt.condition, &(*get_type_info("bool").value()));

                uint32_t then_label = spv.get_id();
                uint32_t else_label = spv.get_id();
                uint32_t merge_label = spv.get_id();

                spv.SelectionMerge(merge_label, spv::SelectionControlMaskNone);
                spv.BranchConditional(
                    condition->load(spv, get_type_info("bool")),
                    then_label,
                    else_label,
                    nullptr,
                    0
                );

                // then block
                spv.Label(then_label);
                reopen_scope(subscope_index);
                auto then_info =
                    generate_executable_block(if_stmt.then_branch, return_variable, then_label);
                if (!then_info.exclude_ending_branch) {
                    spv.Branch(merge_label);
                }

                spv.Label(else_label);
                if (if_stmt.else_branch) {
                    advance_scope();
                    auto else_info = generate_executable_block(
                        *if_stmt.else_branch,
                        return_variable,
                        else_label
                    );
                    if (!else_info.exclude_ending_branch) {
                        spv.Branch(merge_label);
                    }
                } else {
                    spv.Branch(merge_label);
                }

                advance_scope();
                subscope_index = close_scope();

                spv.Label(merge_label);

                uint32_t next_block_label = spv.get_id();

                spv.Branch(next_block_label);
                spv.Label(next_block_label);
            } else if (stmt->is<Stmt::For>()) {
                const Stmt::For& for_stmt = stmt->get<Stmt::For>();

                reopen_scope(subscope_index);

                Ref<VariableScopeNode> loop_scope_node =
                    variable_scope_tree.get_at(current_variable_scope_path).value();

                LoopScopeInfo loop_info = loop_scope_node->loop_info.value();

                // Initialize loop variable
                Opt<VariableInstance> loop_var_instance = find_variable(for_stmt.iterator_name);
                assert(loop_var_instance.has_value());

                Ref<TypeInfo> int_type_info = get_type_info("int").value();

                // loop start
                Ref<ExprResult> start_expr_result =
                    generate_expression(*for_stmt.start, &*int_type_info);
                spv.Store(loop_var_instance->id, start_expr_result->load(spv, int_type_info));

                uint32_t loop_header_label = spv.get_id();
                uint32_t loop_condition_label = spv.get_id();
                uint32_t loop_body_label = spv.get_id();
                uint32_t loop_continue_label = loop_info.continue_label_id;
                uint32_t loop_merge_label = loop_info.break_label_id;

                spv.Branch(loop_header_label);

                spv.Label(loop_header_label);
                spv.LoopMerge(loop_merge_label, loop_continue_label, spv::LoopControlMaskNone);

                spv.Branch(loop_condition_label);

                spv.Label(loop_condition_label);

                // loop body
                uint32_t iter_var_value =
                    spv.LoadNew((*loop_var_instance->type)->id, loop_var_instance->id);

                Ref<ExprResult> end_expr_result =
                    generate_expression(*for_stmt.end, &*int_type_info);

                uint32_t condition = spv.SLessThanNew(
                    decl_ids[arena.string_pool.add("bool")],
                    iter_var_value,
                    end_expr_result->load(spv, int_type_info)
                );

                spv.BranchConditional(condition, loop_body_label, loop_merge_label, nullptr, 0);

                spv.Label(loop_body_label);

                auto body_info =
                    generate_executable_block(for_stmt.body, return_variable, loop_body_label);

                if (!body_info.exclude_ending_branch) {
                    spv.Branch(loop_continue_label);
                }

                // loop continue
                spv.Label(loop_continue_label);

                uint32_t iter_step_value =
                    for_stmt.step ? generate_expression(**for_stmt.step, &*int_type_info)
                                        ->load(spv, int_type_info)
                                  : get_constant_int(1);

                uint32_t incremented_value = spv.IAddNew(
                    decl_ids[arena.string_pool.add("int")],
                    iter_var_value,
                    iter_step_value
                );

                spv.Store(loop_var_instance->id, incremented_value);
                spv.Branch(loop_header_label);

                advance_scope();
                subscope_index = close_scope();

                // loop merge
                spv.Label(loop_merge_label);
            } else if (stmt->is<Stmt::Break>()) {
                // Find the nearest loop scope
                std::vector<int32_t> path = current_variable_scope_path;
                Ref<VariableScopeNode> node = variable_scope_tree.get_at(path).value();
                Opt<LoopScopeInfo> loop_info = node->loop_info;

                while (!loop_info.has_value()) {
                    if (path.size() == 0) {
                        assert(false); // break outside of loop
                    }
                    path.pop_back();
                    Opt<Ref<VariableScopeNode>> parent_node = node->parent;

                    if (!parent_node.has_value()) {
                        assert(false); // break outside of loop
                    }

                    node = parent_node.value();
                    loop_info = node->loop_info;
                }

                spv.Branch(loop_info->break_label_id);
                spv.LabelNew(); // create a new label to avoid invalid fallthrough
            } else if (stmt->is<Stmt::Continue>()) {
                // Find the nearest loop scope
                std::vector<int32_t> path = current_variable_scope_path;
                Ref<VariableScopeNode> node = variable_scope_tree.get_at(path).value();
                Opt<LoopScopeInfo> loop_info = node->loop_info;

                while (!loop_info.has_value()) {
                    if (path.size() == 0) {
                        assert(false); // continue outside of loop
                    }
                    path.pop_back();
                    Opt<Ref<VariableScopeNode>> parent_node = node->parent;

                    if (!parent_node.has_value()) {
                        assert(false); // continue outside of loop
                    }

                    node = parent_node.value();
                    loop_info = node->loop_info;
                }

                spv.Branch(loop_info->continue_label_id);
                spv.LabelNew(); // create a new label to avoid invalid fallthrough
            }
        }

        if (!has_return && is_root) {
            if (return_variable) {
                uint32_t value = spv.LoadNew((*return_variable->type)->id, return_variable->id);
                spv.ReturnValue(value);
            } else {
                spv.Return();
            }
        }

        return block_info;
    }

    Ref<ExprResult> generate_expression(const Expr& expr, const TypeInfo* expected_type) {
        return std::visit(
            [this, expected_type](const auto& e) {
                return this->generate_expr(e, expected_type);
            },
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
                case TypeInfo::BuiltinPrimitive::Void:
                    assert(false);
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
                case TypeInfo::BuiltinPrimitive::Void:
                    assert(false);
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

        assert(false);
    }

    Ref<ExprResult> expr_ref(ExprResult&& result) {
        expr_gen_results.emplace(std::move(result));
        return Ref<ExprResult>(&expr_gen_results, expr_gen_results.size() - 1, 1);
    }

    Ref<ExprResult> generate_expr(const Expr::Binary& b, const TypeInfo* expected_type) {
        Ref<ExprResult> left = this->generate_expression(*b.lhs, expected_type);
        Ref<ExprResult> right = this->generate_expression(*b.rhs, &*left->type);

        OpFamily op_family = get_op_family(*left->type, *right->type);

        std::variant<uint32_t, VariableInstance, VectorSwizzle> res;
        Ref<TypeInfo> return_type = left->type;
        switch (b.op) {
            case Expr::BinaryOp::Add:
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FAddNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        res = spv.IAddNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::VectorScalar: {
                        auto& vec_type = left->type->get<TypeInfo::Vector>();
                        if (vec_type.element->get_underlying_primitive().primitive ==
                            TypeInfo::BuiltinPrimitive::Float) {
                            res = spv.FAddNew(
                                left->type->id,
                                left->load(spv),
                                right->load(spv, left->type)
                            );
                        } else {
                            res = spv.IAddNew(
                                left->type->id,
                                left->load(spv),
                                right->load(spv, left->type)
                            );
                        }
                        break;
                    }
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Sub:
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FSubNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        res = spv.ISubNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::VectorScalar: {
                        auto& vec_type = left->type->get<TypeInfo::Vector>();
                        if (vec_type.element->get_underlying_primitive().primitive ==
                            TypeInfo::BuiltinPrimitive::Float) {
                            res = spv.FSubNew(
                                left->type->id,
                                left->load(spv),
                                right->load(spv, left->type)
                            );
                        } else {
                            res = spv.ISubNew(
                                left->type->id,
                                left->load(spv),
                                right->load(spv, left->type)
                            );
                        }
                        break;
                    }
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Mul:
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FMulNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        res = spv.IMulNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::VectorScalar:
                        res = spv.VectorTimesScalarNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv)
                        );
                        break;
                    case OpFamily::MatrixScalar:
                        res = spv.MatrixTimesScalarNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv)
                        );
                        break;
                    case OpFamily::MatrixVector:
                        res = spv.MatrixTimesVectorNew(
                            right->type->id,
                            left->load(spv),
                            right->load(spv)
                        );

                        return_type = right->type;
                        break;
                    case OpFamily::VectorMatrix:
                        res = spv.VectorTimesMatrixNew(
                            right->type->id,
                            left->load(spv),
                            right->load(spv)
                        );

                        return_type = left->type;
                        break;
                    case OpFamily::MatrixMatrix: {
                        const TypeInfo::Matrix& left_matrix =
                            left->type->get<TypeInfo::Matrix>();
                        const TypeInfo::Matrix& right_matrix =
                            right->type->get<TypeInfo::Matrix>();

                        const TypeInfo::Vector& left_vector =
                            left_matrix.vector_element->get<TypeInfo::Vector>();

                        uint32_t right_columns = right_matrix.columns;
                        uint32_t left_rows = left_vector.size;

                        int result_rows = left_rows;
                        int result_columns = right_columns;

                        std::string type_name = left_vector.element->name.to_string() +
                                                std::to_string(result_rows) + "x" +
                                                std::to_string(result_columns);

                        return_type = get_type_info(type_name).value();
                        res = spv.MatrixTimesMatrixNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv)
                        );

                        break;
                    }
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Div:
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FDivNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SDivNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.UDivNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Mod:
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FModNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SModNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.UModNew(
                            left->type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Equal:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        res = spv.IEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Bool:
                        res = spv.LogicalEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::NotEqual:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdNotEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        res = spv.INotEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Bool:
                        res = spv.LogicalNotEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Less:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdLessThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SLessThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.ULessThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::LessEqual:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdLessThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SLessThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.ULessThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::Greater:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdGreaterThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SGreaterThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.UGreaterThanNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::GreaterEqual:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Float:
                        res = spv.FOrdGreaterThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        res = spv.SGreaterThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        res = spv.UGreaterThanEqualNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;

            case Expr::BinaryOp::Or:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Bool:
                        res = spv.LogicalOrNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            case Expr::BinaryOp::And:
                return_type = get_type_info("bool").value();
                switch (op_family) {
                    case OpFamily::Bool:
                        res = spv.LogicalAndNew(
                            return_type->id,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;

            case Expr::BinaryOp::Assign: {
                left->store(spv, right->load(spv, left->type));
                res = left->data;
                break;
            }
            case Expr::BinaryOp::AddAssign: {
                uint32_t add_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FAdd(
                            left->type->id,
                            add_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IAdd(
                            left->type->id,
                            add_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                left->store(spv, add_res);
                res = left->data;
                break;
            }
            case Expr::BinaryOp::SubAssign: {
                uint32_t sub_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FSub(
                            left->type->id,
                            sub_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.ISub(
                            left->type->id,
                            sub_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                left->store(spv, sub_res);
                res = left->data;
                break;
            }
            case Expr::BinaryOp::MulAssign: {
                uint32_t mul_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FMul(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                    case OpFamily::Uint:
                        spv.IMul(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::VectorScalar:
                        spv.VectorTimesScalar(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::MatrixScalar:
                        spv.MatrixTimesScalar(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::MatrixVector:
                        spv.MatrixTimesVector(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::VectorMatrix:
                        spv.VectorTimesMatrix(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::MatrixMatrix:
                        spv.MatrixTimesMatrix(
                            left->type->id,
                            mul_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                left->store(spv, mul_res);
                res = left->data;
                break;
            }
            case Expr::BinaryOp::DivAssign: {
                uint32_t div_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Float:
                        spv.FDiv(
                            left->type->id,
                            div_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Int:
                        spv.SDiv(
                            left->type->id,
                            div_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        spv.UDiv(
                            left->type->id,
                            div_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                left->store(spv, div_res);
                res = left->data;
                break;
            }
            case Expr::BinaryOp::ModAssign: {
                uint32_t mod_res = spv.get_id();
                switch (op_family) {
                    case OpFamily::Int:
                        spv.SMod(
                            left->type->id,
                            mod_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    case OpFamily::Uint:
                        spv.UMod(
                            left->type->id,
                            mod_res,
                            left->load(spv),
                            right->load(spv, left->type)
                        );
                        break;
                    default:
                        assert(false);
                        break;
                }
                left->store(spv, mod_res);
                res = left->data;
                break;
            }
        }

        return expr_ref({ res, return_type });
    }

    Ref<ExprResult> generate_expr(const Expr::Unary& u, const TypeInfo* expected_type) {
        Ref<ExprResult> inner = generate_expression(*u.expr, expected_type);
        TypeInfo::BuiltinPrimitive underlying_primitive =
            inner->type->get_underlying_primitive().primitive;
        uint32_t res = spv.get_id();
        switch (u.op) {
            case Expr::UnaryOp::Neg:
                switch (underlying_primitive) {
                    case TypeInfo::BuiltinPrimitive::Float:
                        spv.FNegate(inner->type->id, res, inner->load(spv));
                        break;
                    case TypeInfo::BuiltinPrimitive::Int:
                        spv.SNegate(inner->type->id, res, inner->load(spv));
                        break;
                    default:
                        assert(false);
                }
                break;
            case Expr::UnaryOp::Not:
                switch (underlying_primitive) {
                    case TypeInfo::BuiltinPrimitive::Bool:
                        spv.LogicalNot(inner->type->id, res, inner->load(spv));
                        break;
                    default:
                        assert(false);
                }
                break;
        }

        return expr_ref({ res, inner->type });
    }

    Ref<ExprResult> generate_expr(const Expr::Call& c, const TypeInfo*) {
        std::vector<uint32_t> ops;
        Opt<Ref<Decl>> fun_decl = try_find_function(c.name.name);

        if (!fun_decl.has_value()) {
            int function_id = 0;

            for (size_t i = 0; i < builtin_functions.size(); i++) {
                if (c.name.name == builtin_functions[i].name) {
                    function_id = i;
                    break;
                }
            }

            const BuiltinFunction& builtin = builtin_functions[function_id];
            Opt<Ref<TypeInfo>> first_type = std::nullopt;

            std::vector<Ref<TypeInfo>> arg_types;
            std::vector<uint32_t> args;
            for (size_t i = 0; i < c.args.size(); i++) {
                const auto& arg = c.args[i];
                switch (builtin.input_kind) {
                    case BuiltinInputKind::Static: {
                        if (first_type == std::nullopt) {
                            Opt<Ref<TypeInfo>> first_type_target;
                            if (builtin.inputs.size() == 1) {
                                first_type_target = get_type_info(builtin.inputs[0][0]);
                            }

                            auto arg_result = generate_expression(
                                *arg,
                                first_type_target ? &**first_type_target : nullptr
                            );
                            if (i == 0) {
                                first_type = arg_result->type;
                            }
                            arg_types.push_back(arg_result->type);
                            args.push_back(arg_result->load(spv, first_type_target));
                        } else {
                            int best_fit_index = -1;
                            for (size_t j = 0; j < builtin.inputs.size(); j++) {
                                if (*first_type ==
                                    get_type_info(builtin.inputs[j][0]).value()) {
                                    best_fit_index = j;
                                    break;
                                }
                            }
                            if (best_fit_index == -1) {
                                assert(false);
                            }

                            auto arg_result = generate_expression(
                                *arg,
                                &**get_type_info(builtin.inputs[best_fit_index][i])
                            );

                            arg_types.push_back(arg_result->type);

                            args.push_back(arg_result->load(
                                spv,
                                get_type_info(builtin.inputs[best_fit_index][i])
                            ));
                            break;
                        }
                        break;
                    }
                    case BuiltinInputKind::Vectorized:
                    case BuiltinInputKind::Custom:
                    case BuiltinInputKind::Packed: {
                        auto arg_result = generate_expression(*arg, nullptr);
                        if (i == 0) {
                            first_type = arg_result->type;
                        }
                        arg_types.push_back(arg_result->type);
                        args.push_back(arg_result->load(spv, std::nullopt));
                        break;
                    }
                }
            }

            Opt<Ref<TypeInfo>> return_type = std::nullopt;

            // custom input has its own type
            if (builtin.input_kind == BuiltinInputKind::Custom) {
                return_type = builtin.custom_input(arena, arg_types);
            } else {
                switch (builtin.output_kind) {
                    case BuiltinOutputKind::Static: {
                        return_type = get_type_info(builtin.static_output);
                        break;
                    }
                    case BuiltinOutputKind::InheritedSingle: {
                        return_type = get_type_info(
                            TypeInfo::builtin_primitive_str(
                                (*first_type)->get_underlying_primitive().primitive
                            )
                        );
                        break;
                    }
                    case BuiltinOutputKind::StaticVectorized: {
                        uint32_t vector_size = (*first_type)->get<TypeInfo::Vector>().size;

                        return_type = get_type_info(
                            TypeInfo::builtin_primitive_str(builtin.static_output_base) +
                            std::to_string(vector_size)
                        );
                        break;
                    }
                    case BuiltinOutputKind::Inherited: {
                        return_type = first_type;
                        break;
                    }
                }
            }

            uint32_t res = builtin_function(
                spv,
                **return_type,
                c.name.name,
                this->glsl_ext,
                arg_types,
                args
            );

            return expr_ref({ res, *return_type });
        } else {
            const Decl::Function& fun = fun_decl.value()->get<Decl::Function>();
            for (size_t i = 0; i < fun.params.size(); i++) {
                const auto& arg = c.args[i];
                const auto& target = fun.params[i].type.resolved_type;
                auto arg_result = generate_expression(*arg, &**target);
                ops.push_back(arg_result->load(spv, target));
            }
            uint32_t res = spv.get_id();
            spv.FunctionCall(
                fun.return_type_id,
                res,
                decl_ids[fun.name.name],
                ops.data(),
                ops.size()
            );
            return expr_ref({ res, *fun.ret.type.resolved_type });
        }
    }

    Ref<ExprResult> generate_expr(const Expr::ListAccess& la, const TypeInfo*) {
        Ref<ExprResult> list = generate_expression(*la.list, nullptr);
        Ref<ExprResult> index = generate_expression(*la.index, &**get_type_info("int"));
        uint32_t res = spv.get_id();
        uint32_t index_id = index->load(spv, get_type_info("int"));
        spv.AccessChain(list->type->id, res, list->load(spv), &index_id, 1);
        assert(list->type->is<TypeInfo::Array>());
        TypeInfo::Array& array = list->type->get<TypeInfo::Array>();
        return expr_ref({ res, array.element });
    }

    uint32_t component_from_char(char c) {
        switch (c) {
            case 'x':
            case 'r':
                return 0;
            case 'y':
            case 'g':
                return 1;
            case 'z':
            case 'b':
                return 2;
            case 'w':
            case 'a':
                return 3;
            default:
                assert(false);
        }
    }

    Ref<ExprResult> generate_expr(const Expr::FieldAccess& fa, const TypeInfo*) {
        Ref<ExprResult> base = generate_expression(*fa.object, nullptr);
        uint32_t ptr_res = spv.get_id();

        if (base->type->is<TypeInfo::Struct>()) {
            const TypeInfo::Struct& s = base->type->get<TypeInfo::Struct>();
            VariableInstance base_variable = std::get<VariableInstance>(base->data);
            VariableInstance virtual_field_variable;
            for (size_t i = 0; i < s.members.size(); i++) {
                if (s.members[i].name == fa.field.name) {
                    uint32_t constant_id = get_constant_int(i);

                    spv.AccessChain(
                        s.members[i].type->get_pointer_type(*base_variable.storage_class),
                        ptr_res,
                        base_variable.id,
                        &constant_id,
                        1
                    );
                    virtual_field_variable = { ptr_res,
                                               s.members[i].type,
                                               base_variable.storage_class };
                    break;
                }
            }

            return expr_ref({ virtual_field_variable, *virtual_field_variable.type });
        } else if (base->type->is<TypeInfo::Vector>()) {
            const TypeInfo::Vector& v = base->type->get<TypeInfo::Vector>();
            if (fa.field.name.size() == 1) {
                char index_c = fa.field.name.c_str()[0];
                uint32_t index = component_from_char(index_c);

                bool is_variable = base->data.index() == 1;
                if (is_variable) {
                    VariableInstance base_variable = std::get<VariableInstance>(base->data);
                    uint32_t constant_id = get_constant_int(index);

                    if (base_variable.storage_class) {
                        spv.AccessChain(
                            v.element->get_pointer_type(*base_variable.storage_class),
                            ptr_res,
                            base_variable.id,
                            &constant_id,
                            1
                        );

                        VariableInstance virtual_field_variable = {
                            ptr_res,
                            v.element,
                            base_variable.storage_class,
                        };

                        return expr_ref(
                            { virtual_field_variable, *virtual_field_variable.type }
                        );
                    }
                }

                uint32_t res =
                    spv.CompositeExtractNew(v.element->id, base->load(spv), &index, 1);

                return expr_ref({ res, v.element });

            } else {
                // this is a swizzle

                std::vector<uint32_t> indices;
                for (char c : fa.field.name.to_string()) {
                    uint32_t index = 0;
                    switch (c) {
                        case 'x':
                        case 'r':
                            index = 0;
                            break;
                        case 'y':
                        case 'g':
                            index = 1;
                            break;
                        case 'z':
                        case 'b':
                            index = 2;
                            break;
                        case 'w':
                        case 'a':
                            index = 3;
                            break;
                        default:
                            assert(false);
                    }
                    indices.push_back(index);
                }

                Ref<TypeInfo> result_type =
                    indices.size() == 1
                        ? v.element
                        : *get_type_info(
                              v.element->name.to_string() + std::to_string(indices.size())
                          );

                std::vector<uint32_t> constants;
                for (uint32_t index : indices) {
                    constants.push_back(get_constant_int(index));
                }

                VectorSwizzle res(base, indices, constants);

                return expr_ref({ res, result_type });
            }
        } else if (base->type->is<TypeInfo::Matrix>()) {
            const TypeInfo::Matrix& m = base->type->get<TypeInfo::Matrix>();
            const TypeInfo::Vector& v = m.vector_element->get<TypeInfo::Vector>();
            assert(fa.field.name.size() == 2);
            char c_index_c = fa.field.name.c_str()[1];
            char r_index_c = fa.field.name.c_str()[0];
            uint32_t c_index = c_index_c - '0';
            uint32_t r_index = component_from_char(r_index_c);

            bool is_variable = base->data.index() == 1;
            if (is_variable) {
                VariableInstance base_variable = std::get<VariableInstance>(base->data);
                uint32_t c_constant_id = get_constant_int(c_index);
                uint32_t r_constant_id = get_constant_int(r_index);

                uint32_t chain[2] = { c_constant_id, r_constant_id };

                if (base_variable.storage_class) {
                    spv.AccessChain(
                        v.element->get_pointer_type(*base_variable.storage_class),
                        ptr_res,
                        base_variable.id,
                        chain,
                        2
                    );

                    VariableInstance virtual_field_variable = {
                        ptr_res,
                        v.element,
                        base_variable.storage_class,
                    };

                    return expr_ref({ virtual_field_variable, *virtual_field_variable.type });
                }
            }

            uint32_t indices[2] = { c_index, r_index };

            uint32_t res = spv.CompositeExtractNew(v.element->id, base->load(spv), indices, 2);

            return expr_ref({ res, v.element });
        }

        assert(false && "Field access on non-struct/non-vector/non-matrix type");
    }

    Ref<ExprResult>
    generate_expr(const Expr::NumberLiteral& nl, const TypeInfo* expected_type) {
        double v = nl.value;
        bool could_be_int = v == static_cast<int>(v);
        bool could_be_uint = v == static_cast<uint32_t>(v);

        if (expected_type != nullptr) {
            if (expected_type->is<TypeInfo::Primitive>()) {
                const TypeInfo::Primitive& primitive =
                    expected_type->get<TypeInfo::Primitive>();
                if (primitive.primitive == TypeInfo::BuiltinPrimitive::Int && could_be_int) {
                    return expr_ref(
                        {
                            get_constant_int(static_cast<int>(v)),
                            *get_type_info("int"),
                        }
                    );
                } else if (
                    primitive.primitive == TypeInfo::BuiltinPrimitive::Uint && could_be_uint
                ) {
                    return expr_ref(
                        {
                            get_constant_uint(static_cast<uint32_t>(v)),
                            *get_type_info("uint"),
                        }
                    );
                }
            }
        }
        return expr_ref({ get_constant_float(v), *get_type_info("float") });
    }

    Ref<ExprResult> generate_expr(const Expr::VariableAccess& va, const TypeInfo*) {
        VariableInstance var = *find_variable(va.name.name);
        return expr_ref({ var, *var.type });
    }

    void flush(std::ostream& out) {
        for (unsigned i = 0; i < spv.words.size(); i++) {
            out.write(reinterpret_cast<const char*>(&spv.words[i]), sizeof(uint32_t));
        }
    }

    void flush(std::vector<char>& out) {
        out.reserve(spv.words.size() * 4);
        for (unsigned i = 0; i < spv.words.size(); i++) {
            for (unsigned j = 0; j < sizeof(uint32_t); j++) {
                out.push_back(reinterpret_cast<const char*>(&spv.words[i])[j]);
            }
        }
    }
    void flush(std::vector<uint32_t>& out) {
        out.reserve(spv.words.size() * 4);
        out.insert(out.end(), spv.words.begin(), spv.words.end());
    }
};
} // namespace lesl
