#pragma once

#include "log.hpp"
#include "utils.hpp"
#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include "spirv_binary_container.hpp"

#include "repr.hpp"
#include "arena.hpp"
#include "stringpool.hpp"

#include <vector>

enum class BuiltinInputKind {
    Static,
    Vectorized,
    Packed,
};

enum class BuiltinOutputKind {
    // all overloads have the same output type
    Static,
    // output type is the base type of vector arguments
    InheritedSingle,
    // output type is a static base type, vectorized to arguments
    StaticVectorized,
    // output type is fully inherited
    Inherited,
};

struct BuiltinFunction {
    const char* name;

    BuiltinInputKind input_kind;
    BuiltinOutputKind output_kind;

    // INPUT
    //  Static
    std::vector<std::vector<const char*>> inputs;
    //  Vectorized
    std::vector<TypeInfo::BuiltinPrimitive> allowed_primitive_inputs;
    uint32_t min_vector_size;
    uint32_t max_vector_size;
    //  Packed
    TypeInfo::BuiltinPrimitive base_input_primitive;
    uint32_t required_packed_input;

    // OUTPUT
    //  Static
    const char* static_output;
    //  Vectorized
    TypeInfo::BuiltinPrimitive static_output_base;

    BuiltinFunction(const char* name) : name(name) {}

    BuiltinFunction& with_static_input(std::vector<std::vector<const char*>>&& inputs) {
        this->input_kind = BuiltinInputKind::Static;
        this->inputs = std::move(inputs);

        return *this;
    }

    BuiltinFunction& with_vectorized_input(
        std::vector<TypeInfo::BuiltinPrimitive>&& allowed_bases,
        uint32_t min_vector_size,
        uint32_t max_vector_size
    ) {
        assert(min_vector_size >= 1 && min_vector_size <= 4);
        assert(max_vector_size >= 1 && max_vector_size <= 4);
        this->input_kind = BuiltinInputKind::Vectorized;

        this->allowed_primitive_inputs = std::move(allowed_bases);
        this->min_vector_size = min_vector_size;
        this->max_vector_size = max_vector_size;

        return *this;
    }

    BuiltinFunction& with_packed_input(
        TypeInfo::BuiltinPrimitive base_input_primitive,
        uint32_t required_packed_inputs
    ) {
        assert(required_packed_inputs >= 1 && required_packed_inputs <= 4);
        this->input_kind = BuiltinInputKind::Packed;

        this->base_input_primitive = base_input_primitive;
        this->required_packed_input = required_packed_inputs;

        return *this;
    }

    BuiltinFunction& with_static_output(const char* name) {
        this->output_kind = BuiltinOutputKind::Static;

        this->static_output = name;

        return *this;
    }

    BuiltinFunction& with_inherited_output() {
        this->output_kind = BuiltinOutputKind::Inherited;

        return *this;
    }

    BuiltinFunction&
    with_static_vectorized_output(TypeInfo::BuiltinPrimitive vectorized_output_base) {
        this->output_kind = BuiltinOutputKind::StaticVectorized;

        this->static_output_base;

        return *this;
    }

    BuiltinFunction& with_inherited_single_output() {
        this->output_kind = BuiltinOutputKind::InheritedSingle;

        return *this;
    }
};

inline static const std::vector<BuiltinFunction> builtin_functions = {
    // triganometric functions
    BuiltinFunction("sin")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("cos")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("tan")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("asin")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("acos")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("atan")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output(),
    BuiltinFunction("atan2")
        .with_static_input({ { "float", "float" } })
        .with_static_output("float"),
    // basic math or vector math
    BuiltinFunction("abs")
        .with_vectorized_input(
            { TypeInfo::BuiltinPrimitive::Float, TypeInfo::BuiltinPrimitive::Int },
            1,
            4
        )
        .with_inherited_output(),
    BuiltinFunction("sqrt").with_static_input({ { "float" } }).with_static_output("float"),
    BuiltinFunction("length")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 2, 4)
        .with_static_output("float"),
    BuiltinFunction("dot")
        .with_static_input(
            {
                { "float2", "float2" },
                { "float3", "float3" },
                { "float4", "float4" },
            }
        )
        .with_static_output("float"),
    BuiltinFunction("cross")
        .with_static_input(
            {
                { "float3", "float3" },
            }
        )
        .with_static_output("float"),
    BuiltinFunction("normalize")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 2, 4)
        .with_inherited_output(),
    // interpolation
    BuiltinFunction("clamp")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "int", "int", "int" },
                { "uint", "uint", "uint" },
            }
        )
        .with_inherited_output(),
    BuiltinFunction("max")
        .with_static_input(
            {
                { "float", "float" },
                { "int", "int" },
                { "uint", "uint" },
            }
        )
        .with_inherited_output(),
    BuiltinFunction("min")
        .with_static_input(
            {
                { "float", "float" },
                { "int", "int" },
                { "uint", "uint" },
            }
        )
        .with_inherited_output(),
    BuiltinFunction("lerp")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "float2", "float2", "float" },
                { "float3", "float3", "float" },
                { "float3", "float4", "float" },
            }
        )
        .with_inherited_output(),
    BuiltinFunction("smoothstep")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "float2", "float2", "float" },
                { "float3", "float3", "float" },
                { "float3", "float4", "float" },
            }
        )
        .with_inherited_output(),
    // builtin constructors
    BuiltinFunction("float2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 2)
        .with_static_output("float2"),
    BuiltinFunction("float3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 3)
        .with_static_output("float3"),
    BuiltinFunction("float4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 4)
        .with_static_output("float4"),
    BuiltinFunction("int2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 2)
        .with_static_output("int2"),
    BuiltinFunction("int3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 3)
        .with_static_output("int3"),
    BuiltinFunction("int4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 4)
        .with_static_output("int4"),
    BuiltinFunction("uint2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 2)
        .with_static_output("uint2"),
    BuiltinFunction("uint3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 3)
        .with_static_output("uint3"),
    BuiltinFunction("uint4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 4)
        .with_static_output("uint4"),
    BuiltinFunction("bool2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 2)
        .with_static_output("bool2"),
    BuiltinFunction("bool3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 3)
        .with_static_output("bool3"),
    BuiltinFunction("bool4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 4)
        .with_static_output("bool4"),
};

inline static uint32_t builtin_function(
    spv_binary::BinaryContainer& spv,
    const TypeInfo& res_type_info,
    const PoolStr& name,
    uint32_t glsl_std_id,
    std::vector<uint32_t> args
) {
    uint32_t res_type = res_type_info.id;
    auto primitive = res_type_info.get_underlying_primitive().primitive;

    if (name == "sin") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Sin, args.data(), args.size());
    } else if (name == "cos") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Cos, args.data(), args.size());
    } else if (name == "tan") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Tan, args.data(), args.size());
    } else if (name == "asin") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Asin, args.data(), args.size());
    } else if (name == "acos") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Acos, args.data(), args.size());
    } else if (name == "atan") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Atan, args.data(), args.size());
    } else if (name == "atan2") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Atan2, args.data(), args.size());
    } else if (name == "sqrt") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Sqrt, args.data(), args.size());
    } else if (name == "length") {
        return spv
            .ExtInstNew(res_type, glsl_std_id, GLSLstd450Length, args.data(), args.size());
    } else if (name == "dot") {
        return spv.DotNew(res_type, args[0], args[1]);
    } else if (name == "cross") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450Cross, args.data(), args.size());
    } else if (name == "normalize") {
        return spv
            .ExtInstNew(res_type, glsl_std_id, GLSLstd450Normalize, args.data(), args.size());
    } else if (name == "clamp") {
        switch (primitive) {
            case TypeInfo::BuiltinPrimitive::Float:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450FClamp,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Uint:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450UClamp,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Int:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450SClamp,
                    args.data(),
                    args.size()
                );
        }
    } else if (name == "max") {
        switch (primitive) {
            case TypeInfo::BuiltinPrimitive::Float:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450FMax,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Uint:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450UMax,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Int:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450SMax,
                    args.data(),
                    args.size()
                );
        }
    } else if (name == "min") {
        switch (primitive) {
            case TypeInfo::BuiltinPrimitive::Float:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450FMin,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Uint:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450UMin,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Int:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450SMin,
                    args.data(),
                    args.size()
                );
        }
    } else if (name == "lerp") {
        return spv.ExtInstNew(res_type, glsl_std_id, GLSLstd450FMix, args.data(), args.size());
    } else if (name == "smoothstep") {
        return spv
            .ExtInstNew(res_type, glsl_std_id, GLSLstd450SmoothStep, args.data(), args.size());
    } else if (name == "abs") {
        switch (primitive) {
            case TypeInfo::BuiltinPrimitive::Float:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450FAbs,
                    args.data(),
                    args.size()
                );
            case TypeInfo::BuiltinPrimitive::Int:
                return spv.ExtInstNew(
                    res_type,
                    glsl_std_id,
                    GLSLstd450SAbs,
                    args.data(),
                    args.size()
                );
        }
    } else if (name == "float2" || name == "float3" || name == "float4" || name == "int2" ||
               name == "int3" || name == "int4" || name == "uint2" || name == "uint3" ||
               name == "uint4") {
        if (args.size() == 1) {
            std::vector<uint32_t> args_copy;
            std::string name_str = name.to_string();
            uint32_t vector_size = name_str[name_str.size() - 1] - '0';
            args_copy.reserve(vector_size);
            for (uint32_t i = 0; i < vector_size; i++) {
                args_copy.push_back(args[0]);
            }

            return spv.CompositeConstructNew(res_type, args_copy.data(), args_copy.size());
        } else {
            return spv.CompositeConstructNew(res_type, args.data(), args.size());
        }
    }

    return 0;
}
