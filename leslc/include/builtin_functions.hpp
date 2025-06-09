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

struct BuiltinOverload {
    std::vector<const char*> arg_types;
    const char* return_type;
};

struct BuiltinFunction {
    const char* name;
    std::vector<BuiltinOverload> overloads;
};

inline static const std::vector<BuiltinFunction> builtin_functions = {
    // triganometric functions
    {
        "sin",
        {
            { { "float" }, "float" },
            { { "float2" }, "float2" },
            { { "float3" }, "float3" },
            { { "float4" }, "float4" },
        },
    },
    {
        "cos",
        {
            { { "float" }, "float" },
            { { "float2" }, "float2" },
            { { "float3" }, "float3" },
            { { "float4" }, "float4" },
        },
    },
    {
        "tan",
        {
            { { "float" }, "float" },
            { { "float2" }, "float2" },
            { { "float3" }, "float3" },
            { { "float4" }, "float4" },
        },
    },
    // basic math or vector math
    {
        "abs",
        {
            { { "int" }, "int" },
            { { "float" }, "float" },
        },
    },
    {
        "sqrt",
        {
            { { "float" }, "float" },
        },
    },
    {
        "length",
        {
            { { "float2" }, "float" },
            { { "float3" }, "float" },
            { { "float4" }, "float" },
        },
    },
    {
        "dot",
        {
            { { "float2", "float2" }, "float" },
            { { "float3", "float3" }, "float" },
            { { "float4", "float4" }, "float" },
        },
    },
    {
        "cross",
        {
            { { "float3", "float3" }, "float3" },
        },
    },
    {
        "normalize",
        {
            { { "float2" }, "float2" },
            { { "float3" }, "float3" },
            { { "float4" }, "float4" },
        },
    },
    // interpolation
    {
        "clamp",
        {
            { { "float", "float", "float" }, "float" },
            { { "int", "int", "int" }, "int" },
            { { "uint", "uint", "uint" }, "uint" },
        },
    },
    {
        "lerp",
        {
            { { "float", "float", "float" }, "float" },
            { { "float2", "float2", "float" }, "float2" },
            { { "float3", "float3", "float" }, "float3" },
            { { "float4", "float4", "float" }, "float4" },
        },
    },
    {
        "smoothstep",
        {
            { { "float", "float", "float" }, "float" },
            { { "float2", "float2", "float" }, "float2" },
            { { "float3", "float3", "float" }, "float3" },
            { { "float4", "float4", "float" }, "float4" },
        },
    },
    // builtin constructors
    {
        "float2",
        {
            // constructors
            { { "float", "float" }, "float2" },
            { { "float" }, "float2" },
        },
    },
    {
        "float3",
        {
            // constructors
            { { "float", "float", "float" }, "float3" },
            { { "float2", "float" }, "float3" },
            { { "float" }, "float3" },
        },
    },
    {
        "float4",
        {
            // constructors
            { { "float", "float", "float", "float" }, "float4" },
            { { "float2 ", " float ", "float" }, "float4" },
            { { "float3", "float" }, "float4" },
            { { "float" }, "float4" },
        },
    },
    {
        "int2",
        {
            // constructors
            { { "int", "int" }, "int2" },
            { { "int" }, "int2" },
        },
    },
    {
        "int3",
        {
            // constructors
            { { "int", "int", "int" }, "int3" },
            { { "int2", "int" }, "int3" },
            { { "int" }, "int3" },
        },
    },
    {
        "int4",
        {
            // constructors
            { { "int", "int", "int", "int" }, "int4" },
            { { "int2 ", " int ", " int" }, "int4" },
            { { "int3", "int" }, "int4" },
            { { "int" }, "int4" },
        },
    },
    {
        "uint2",
        {
            // constructors
            { { "uint", "uint" }, "uint2" },
            { { "uint" }, "uint2" },
        },
    },
    {
        "uint3",
        {
            // constructors
            { { "uint", "uint", "uint" }, "uint3" },
            { { "uint2", "uint" }, "uint3" },
            { { "uint" }, "uint3" },
        },
    },
    {
        "uint4",
        {
            // constructors
            { { "uint", "uint", "uint", "uint" }, "uint4" },
            { { "uint2 ", " uint ", " uint" }, "uint4" },
            { { "uint3", "uint" }, "uint4" },
            { { "uint" }, "uint4" },
        },
    },
    {
        "bool2",
        {
            // constructors
            { { "bool", "bool" }, "bool2" },
        },
    },
    {
        "bool3",
        {
            // constructors
            { { "bool", "bool", "bool" }, "bool3" },
            { { "bool2", "bool" }, "bool3" },
        },
    },
    {
        "bool4",
        {
            // constructors
            { { "bool", "bool", "bool", "bool" }, "bool4" },
            { { "bool2 ", " bool ", " bool" }, "bool4" },
            { { "bool3", "bool" }, "bool4" },
        },
    },
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
