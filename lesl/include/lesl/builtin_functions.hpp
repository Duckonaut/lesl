#pragma once

#include "spirv/1.0/spirv.hpp"
#include "spirv/1.0/GLSL.std.450.h"

#include "spirv_binary_container.hpp"

#include "lesl/repr.hpp"
#include "stringpool.hpp"

#include <iostream>
#include <vector>
#include <functional>

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

enum class BuiltinEncoding {
    GLSLStd450,
    GLSLStd450TypeDependent,
    Custom
};

using CustomEncoderFunction = std::function<uint32_t(
    spv_binary::BinaryContainer& spv,
    const TypeInfo& res_type_info,
    const PoolStr& name,
    uint32_t glsl_std_opcode,
    std::vector<Ref<TypeInfo>> arg_types,
    std::vector<uint32_t> args
)>;

struct BuiltinFunction {
    const char* name;

    BuiltinInputKind input_kind;
    BuiltinOutputKind output_kind;
    BuiltinEncoding encoding;

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

    // ENCODING
    GLSLstd450 glsl_std_opcode;
    std::unordered_map<TypeInfo::BuiltinPrimitive, GLSLstd450> glsl_std_type_dependent_opcodes;
    CustomEncoderFunction custom_encoder;

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

        this->static_output_base = vectorized_output_base;

        return *this;
    }

    BuiltinFunction& with_inherited_single_output() {
        this->output_kind = BuiltinOutputKind::InheritedSingle;

        return *this;
    }

    BuiltinFunction& with_glsl_std450_encoding(GLSLstd450 opcode) {
        this->encoding = BuiltinEncoding::GLSLStd450;
        this->glsl_std_opcode = opcode;

        return *this;
    }

    BuiltinFunction& with_glsl_std450_type_dependent_encoding(
        std::unordered_map<TypeInfo::BuiltinPrimitive, GLSLstd450>&& opcodes
    ) {
        this->encoding = BuiltinEncoding::GLSLStd450TypeDependent;
        this->glsl_std_type_dependent_opcodes = std::move(opcodes);
        return *this;
    }

    BuiltinFunction& with_custom_encoding(CustomEncoderFunction encoder) {
        this->encoding = BuiltinEncoding::Custom;
        this->custom_encoder = encoder;

        return *this;
    }
};

inline static uint32_t composite_constructor(
    spv_binary::BinaryContainer& spv,
    const TypeInfo& res_type_info,
    const PoolStr& name,
    uint32_t,
    std::vector<Ref<TypeInfo>>,
    std::vector<uint32_t> args
) {
    uint32_t res_type = res_type_info.id;

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

inline static uint32_t primitive_converter(
    spv_binary::BinaryContainer& spv,
    const TypeInfo& res_type_info,
    const PoolStr& name,
    uint32_t,
    std::vector<Ref<TypeInfo>> arg_types,
    std::vector<uint32_t> args
) {
    TypeInfo::BuiltinPrimitive target_primitive =
        res_type_info.get_underlying_primitive().primitive;

    TypeInfo::BuiltinPrimitive source_primitive =
        arg_types[0]->get_underlying_primitive().primitive;

    if (source_primitive == target_primitive) {
        return args[0];
    }

    uint32_t res = spv.get_id();

    switch (target_primitive) {
        case TypeInfo::BuiltinPrimitive::Float:
            switch (source_primitive) {
                case TypeInfo::BuiltinPrimitive::Int:
                    spv.ConvertSToF(res_type_info.id, res, args[0]);
                    break;
                case TypeInfo::BuiltinPrimitive::Uint:
                    spv.ConvertUToF(res_type_info.id, res, args[0]);
                    break;
                default:
                    assert(false);
            }
            break;
        case TypeInfo::BuiltinPrimitive::Int:
            switch (source_primitive) {
                case TypeInfo::BuiltinPrimitive::Float:
                    spv.ConvertFToS(res_type_info.id, res, args[0]);
                    break;
                default:
                    assert(false);
            }
            break;
        case TypeInfo::BuiltinPrimitive::Uint:
            switch (source_primitive) {
                case TypeInfo::BuiltinPrimitive::Float:
                    spv.ConvertFToU(res_type_info.id, res, args[0]);
                    break;
                default:
                    assert(false);
            }
            break;
        default:
            assert(false);
    }

    return res;
}

inline static const std::vector<BuiltinFunction> builtin_functions = {
    // basic math
    BuiltinFunction("round")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Round),
    BuiltinFunction("floor")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Floor),
    BuiltinFunction("ceil")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Ceil),
    BuiltinFunction("fract")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Fract),
    BuiltinFunction("trunc")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Trunc),
    BuiltinFunction("sign")
        .with_vectorized_input(
            { TypeInfo::BuiltinPrimitive::Float, TypeInfo::BuiltinPrimitive::Int },
            1,
            4
        )
        .with_inherited_output()
        .with_glsl_std450_type_dependent_encoding(
            {
                { TypeInfo::BuiltinPrimitive::Float, GLSLstd450FSign },
                { TypeInfo::BuiltinPrimitive::Int, GLSLstd450SSign },
            }
        ),
    BuiltinFunction("abs")
        .with_vectorized_input(
            { TypeInfo::BuiltinPrimitive::Float, TypeInfo::BuiltinPrimitive::Int },
            1,
            4
        )
        .with_inherited_output()
        .with_glsl_std450_type_dependent_encoding(
            {
                { TypeInfo::BuiltinPrimitive::Float, GLSLstd450FAbs },
                { TypeInfo::BuiltinPrimitive::Int, GLSLstd450SAbs },
            }
        ),
    // triganometric functions
    BuiltinFunction("radians")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Radians),
    BuiltinFunction("degrees")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Degrees),
    BuiltinFunction("sin")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Sin),
    BuiltinFunction("cos")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Cos),
    BuiltinFunction("tan")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Tan),
    BuiltinFunction("asin")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Asin),
    BuiltinFunction("acos")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Acos),
    BuiltinFunction("atan")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Atan),
    BuiltinFunction("atan2")
        .with_static_input({ { "float", "float" } })
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Atan2),
    BuiltinFunction("sinh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Sinh),
    BuiltinFunction("cosh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Cosh),
    BuiltinFunction("tanh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Tanh),
    BuiltinFunction("asinh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Asinh),
    BuiltinFunction("acosh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Acosh),
    BuiltinFunction("atanh")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Atanh),
    // exponential functions
    BuiltinFunction("pow")
        .with_static_input({ { "float", "float" } })
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Pow),
    BuiltinFunction("exp")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Exp),
    BuiltinFunction("log")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Log),
    BuiltinFunction("exp2")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Exp2),
    BuiltinFunction("log2")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Log2),
    BuiltinFunction("sqrt")
        .with_static_input({ { "float" } })
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Sqrt),
    BuiltinFunction("inverse_sqrt")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 1, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450InverseSqrt),
    // vector math
    BuiltinFunction("length")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 2, 4)
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Length),
    BuiltinFunction("distance")
        .with_static_input(
            {
                { "float2", "float2" },
                { "float3", "float3" },
                { "float4", "float4" },
            }
        )
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Distance),
    BuiltinFunction("dot")
        .with_static_input(
            {
                { "float2", "float2" },
                { "float3", "float3" },
                { "float4", "float4" },
            }
        )
        .with_static_output("float")
        .with_custom_encoding([](spv_binary::BinaryContainer& spv,
                                 const TypeInfo& res_type_info,
                                 const PoolStr&,
                                 uint32_t,
                                 std::vector<Ref<TypeInfo>>,
                                 std::vector<uint32_t> args) {
            uint32_t res_type = res_type_info.id;
            return spv.DotNew(res_type, args[0], args[1]);
        }),
    BuiltinFunction("cross")
        .with_static_input(
            {
                { "float3", "float3" },
            }
        )
        .with_static_output("float3")
        .with_glsl_std450_encoding(GLSLstd450Cross),
    BuiltinFunction("normalize")
        .with_vectorized_input({ TypeInfo::BuiltinPrimitive::Float }, 2, 4)
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Normalize),
    BuiltinFunction("faceforward")
        .with_static_input(
            {
                { "float2", "float2", "float2" },
                { "float3", "float3", "float3" },
                { "float4", "float4", "float4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450FaceForward),
    BuiltinFunction("reflect")
        .with_static_input(
            {
                { "float2", "float2" },
                { "float3", "float3" },
                { "float4", "float4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Reflect),
    BuiltinFunction("refract")
        .with_static_input(
            {
                { "float2", "float2", "float" },
                { "float3", "float3", "float" },
                { "float4", "float4", "float" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Refract),
    // matrix functions
    BuiltinFunction("determinant")
        .with_static_input(
            {
                { "float2x2" },
                { "float3x3" },
                { "float4x4" },
            }
        )
        .with_static_output("float")
        .with_glsl_std450_encoding(GLSLstd450Determinant),
    BuiltinFunction("inverse")
        .with_static_input(
            {
                { "float2x2" },
                { "float3x3" },
                { "float4x4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450MatrixInverse),
    // interpolation
    BuiltinFunction("clamp")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "float2", "float2", "float2" },
                { "float3", "float3", "float3" },
                { "float4", "float4", "float4" },
                { "int", "int", "int" },
                { "int2", "int2", "int2" },
                { "int3", "int3", "int3" },
                { "int4", "int4", "int4" },
                { "uint", "uint", "uint" },
                { "uint2", "uint2", "uint2" },
                { "uint3", "uint3", "uint3" },
                { "uint4", "uint4", "uint4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_type_dependent_encoding(
            {
                { TypeInfo::BuiltinPrimitive::Float, GLSLstd450FClamp },
                { TypeInfo::BuiltinPrimitive::Uint, GLSLstd450UClamp },
                { TypeInfo::BuiltinPrimitive::Int, GLSLstd450SClamp },
            }
        ),
    BuiltinFunction("max")
        .with_static_input(
            {
                { "float", "float" },
                { "int", "int" },
                { "uint", "uint" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_type_dependent_encoding(
            {
                { TypeInfo::BuiltinPrimitive::Float, GLSLstd450FMax },
                { TypeInfo::BuiltinPrimitive::Uint, GLSLstd450UMax },
                { TypeInfo::BuiltinPrimitive::Int, GLSLstd450SMax },
            }
        ),
    BuiltinFunction("min")
        .with_static_input(
            {
                { "float", "float" },
                { "int", "int" },
                { "uint", "uint" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_type_dependent_encoding(
            {
                { TypeInfo::BuiltinPrimitive::Float, GLSLstd450FMin },
                { TypeInfo::BuiltinPrimitive::Uint, GLSLstd450UMin },
                { TypeInfo::BuiltinPrimitive::Int, GLSLstd450SMin },
            }
        ),
    BuiltinFunction("lerp")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "float2", "float2", "float2" },
                { "float3", "float3", "float3" },
                { "float4", "float4", "float4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450FMix),
    BuiltinFunction("step")
        .with_static_input(
            {
                { "float", "float" },
                { "float2", "float2" },
                { "float3", "float3" },
                { "float4", "float4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450Step),
    BuiltinFunction("smoothstep")
        .with_static_input(
            {
                { "float", "float", "float" },
                { "float2", "float2", "floa2" },
                { "float3", "float3", "float3" },
                { "float3", "float4", "float4" },
            }
        )
        .with_inherited_output()
        .with_glsl_std450_encoding(GLSLstd450SmoothStep),
    // builtin constructors
    BuiltinFunction("float2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 2)
        .with_static_output("float2")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("float3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 3)
        .with_static_output("float3")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("float4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Float, 4)
        .with_static_output("float4")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("int2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 2)
        .with_static_output("int2")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("int3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 3)
        .with_static_output("int3")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("int4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Int, 4)
        .with_static_output("int4")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("uint2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 2)
        .with_static_output("uint2")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("uint3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 3)
        .with_static_output("uint3")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("uint4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Uint, 4)
        .with_static_output("uint4")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("bool2")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 2)
        .with_static_output("bool2")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("bool3")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 3)
        .with_static_output("bool3")
        .with_custom_encoding(composite_constructor),
    BuiltinFunction("bool4")
        .with_packed_input(TypeInfo::BuiltinPrimitive::Bool, 4)
        .with_static_output("bool4")
        .with_custom_encoding(composite_constructor),
    // type conversions
    BuiltinFunction("float")
        .with_static_input(
            {
                { "int" },
                { "uint" },
            }
        )
        .with_static_output("float")
        .with_custom_encoding(primitive_converter),
    BuiltinFunction("int")
        .with_static_input(
            {
                { "float" },
            }
        )
        .with_static_output("int")
        .with_custom_encoding(primitive_converter),
    BuiltinFunction("uint")
        .with_static_input(
            {
                { "float" },
            }
        )
        .with_static_output("uint")
        .with_custom_encoding(primitive_converter),
    // image sampling
    BuiltinFunction("sample2D")
        .with_static_input(
            {
                { "sampler2D", "float2" },
            }
        )
        .with_static_output("float4")
        .with_custom_encoding([](spv_binary::BinaryContainer& spv,
                                 const TypeInfo& res_type_info,
                                 const PoolStr&,
                                 uint32_t,
                                 std::vector<Ref<TypeInfo>>,
                                 std::vector<uint32_t> args) {
            uint32_t res_type = res_type_info.id;
            return spv.ImageSampleImplicitLodNew(res_type, args[0], args[1]);
        }),
};

inline static uint32_t builtin_function(
    spv_binary::BinaryContainer& spv,
    const TypeInfo& res_type_info,
    const PoolStr& name,
    uint32_t glsl_std_id,
    std::vector<Ref<TypeInfo>> arg_types,
    std::vector<uint32_t> args
) {
    const BuiltinFunction* builtin = nullptr;
    for (const BuiltinFunction& bf : builtin_functions) {
        if (std::string(bf.name) == name.c_str()) {
            builtin = &bf;
            break;
        }
    }

    assert(builtin != nullptr && "builtin function not found");

    switch (builtin->encoding) {
        case BuiltinEncoding::GLSLStd450:
            return spv.ExtInstNew(
                res_type_info.id,
                glsl_std_id,
                static_cast<uint32_t>(builtin->glsl_std_opcode),
                args.data(),
                args.size()
            );
        case BuiltinEncoding::GLSLStd450TypeDependent: {
            auto primitive = res_type_info.get_underlying_primitive().primitive;

            auto it = builtin->glsl_std_type_dependent_opcodes.find(primitive);
            assert(
                it != builtin->glsl_std_type_dependent_opcodes.end() &&
                "no matching type-dependent opcode found for builtin function"
            );

            return spv.ExtInstNew(
                res_type_info.id,
                glsl_std_id,
                static_cast<uint32_t>(it->second),
                args.data(),
                args.size()
            );
        }
        case BuiltinEncoding::Custom:
            return builtin
                ->custom_encoder(spv, res_type_info, name, glsl_std_id, arg_types, args);
        default:
            assert(false && "unknown builtin function encoding");
            return 0;
    }
}
