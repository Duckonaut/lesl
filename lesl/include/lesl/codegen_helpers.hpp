#pragma once

#include "spirv/1.0/spirv.hpp"

#include "spirv_binary_container.hpp"

#include "lesl/repr.hpp"
#include "lesl/stringpool.hpp"

#include <unordered_map>
#include <variant>

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

struct VariableInstance {
    uint32_t id;
    Opt<spv::StorageClass> storage_class;
    Opt<Ref<TypeInfo>> type;

    VariableInstance() : id(0), storage_class(std::nullopt), type(std::nullopt) {}
    VariableInstance(uint32_t id, Ref<TypeInfo> type, Opt<spv::StorageClass> storage_class) : id(id), storage_class(storage_class), type(type) {}
};

struct LoopScopeInfo {
    uint32_t break_label_id;
    uint32_t continue_label_id;
};

struct VariableScopeNode {
    std::unordered_map<PoolStr, VariableInstance> variables;
    Opt<Ref<VariableScopeNode>> parent;
    std::vector<Ref<VariableScopeNode>> children;
    Opt<LoopScopeInfo> loop_info;
};

struct VariableScopeTree {
    RefContainer<VariableScopeNode> nodes;

    Ref<VariableScopeNode> root;

    VariableScopeTree() : root(Ref<VariableScopeNode>(&nodes, 0, 0)) {
        // Create root node
        root = nodes.emplace(VariableScopeNode{ {}, std::nullopt, {}, std::nullopt });
    }

    Opt<Ref<VariableScopeNode>> get_at(std::vector<int32_t> path) {
        Ref<VariableScopeNode> current = root;
        for (int32_t index : path) {
            if (index < 0 || static_cast<size_t>(index) >= current->children.size()) {
                return std::nullopt;
            }
            current = current->children[index];
        }
        return current;
    }

    Ref<VariableScopeNode> create_node(Opt<Ref<VariableScopeNode>> parent = std::nullopt, Opt<LoopScopeInfo> loop_info = std::nullopt) {
        size_t index = nodes.size();
        nodes.push_back(VariableScopeNode{ {}, parent, {}, loop_info });
        Ref<VariableScopeNode> node(&nodes, index, 0);
        if (parent.has_value()) {
            parent.value()->children.push_back(node);
        }
        return node;
    }

    void clear() {
        nodes.clear();

        root = nodes.emplace(VariableScopeNode{ {}, std::nullopt, {}, std::nullopt });
    }
};

struct BuiltinInfo {
    uint32_t id;
    uint32_t type_id;
    uint32_t pointer_type_id;
    spv::StorageClass storage_class;
};

struct ExprResult;

struct VectorSwizzle {
    Ref<ExprResult> vector;
    uint32_t component_count; // 1, 2, 3 or 4
    uint32_t components[4]; // up to 4 components, unused components are set to 0
    uint32_t
        component_constants[4]; // constants for component storing

    VectorSwizzle(Ref<ExprResult> vector, std::vector<uint32_t> components, std::vector<uint32_t> component_constants)
        : vector(vector), component_count(components.size()) {
        assert(component_count > 0 && component_count <= 4);
        for (uint32_t i = 0; i < component_count; i++) {
            this->components[i] = components[i];
            this->component_constants[i] = component_constants[i];
        }
    }
};

struct ExprResult {
    std::variant<
        uint32_t,         // raw ID
        VariableInstance, // variable
        VectorSwizzle     // vector swizzles are treated differently than variables
        >
        data;
    Ref<TypeInfo> type;

    uint32_t primitive_cast(
        spv_binary::BinaryContainer& spv,
        TypeInfo::BuiltinPrimitive current,
        TypeInfo::BuiltinPrimitive expected,
        uint32_t expected_type_id,
        uint32_t value
    ) const {
        if (current == expected) {
            return value;
        } else if (current == TypeInfo::BuiltinPrimitive::Float &&
                   expected == TypeInfo::BuiltinPrimitive::Int) {
            return spv.ConvertFToSNew(expected_type_id, value);
        } else if (current == TypeInfo::BuiltinPrimitive::Int &&
                   expected == TypeInfo::BuiltinPrimitive::Uint) {
            return spv.SatConvertSToUNew(expected_type_id, value);
        } else if (current == TypeInfo::BuiltinPrimitive::Uint &&
                   expected == TypeInfo::BuiltinPrimitive::Int) {
            return spv.SatConvertUToSNew(expected_type_id, value);
        } else if (current == TypeInfo::BuiltinPrimitive::Float &&
                   expected == TypeInfo::BuiltinPrimitive::Uint) {
            return spv.ConvertFToUNew(expected_type_id, value);
        } else if (current == TypeInfo::BuiltinPrimitive::Int &&
                   expected == TypeInfo::BuiltinPrimitive::Float) {
            return spv.ConvertSToFNew(expected_type_id, value);
        } else if (current == TypeInfo::BuiltinPrimitive::Uint &&
                   expected == TypeInfo::BuiltinPrimitive::Float) {
            return spv.ConvertUToFNew(expected_type_id, value);
        } else {
            assert(false);
        }
    }

    uint32_t load(
        spv_binary::BinaryContainer& spv,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt
    ) const {
        uint32_t loaded_id = 0;
        if (std::holds_alternative<uint32_t>(data)) {
            loaded_id = std::get<uint32_t>(data);
        } else if (std::holds_alternative<VariableInstance>(data)) {
            const VariableInstance& var = std::get<VariableInstance>(data);
            if (var.storage_class) {
                loaded_id = spv.LoadNew((*var.type)->id, var.id);
            } else {
                loaded_id = var.id;
            }
        } else if (std::holds_alternative<VectorSwizzle>(data)) {
            const VectorSwizzle& swizzle = std::get<VectorSwizzle>(data);
            Ref<ExprResult> inner = swizzle.vector;
            uint32_t base = inner->load(spv);

            loaded_id = spv.VectorShuffleNew(
                type->id,
                base,
                base,
                const_cast<uint32_t*>(swizzle.components),
                swizzle.component_count
            );
        } else {
            assert(false); // Should not reach here
        }

        if (!expected_type.has_value()) {
            return loaded_id;
        }

        // Try to automatically convert the loaded ID to the expected type

        if (expected_type.value() == type) {
            return loaded_id;
        }

        const TypeInfo& expected = *expected_type.value();

        // Rules of conversion:
        // - Primitives can be converted between eachother in the following order:
        //   float -> int -> uint
        //   bools are not convertible and can only be used in boolean expressions
        // - Primitives can be converted to vectors of any size with the same type
        //   This can happen after the primitive conversion, so an int -> float -> float4
        //   automatic conversion is possible All vector elements will be set to the
        //   converted primitive value
        // - Vector sizes cannot be converted between eachother.
        //   Changing a size of a vector is unclear and should be done explicitly
        //   either via a constructor or a swizzle
        // - Vectors can be converted to vectors of different types, but same size
        //   Same rules apply as for primitives, so float4 -> int4 -> uint4
        // - Matrices cannot be converted between eachother

        if (expected.is<TypeInfo::Array>() || expected.is<TypeInfo::Matrix>() ||
            expected.is<TypeInfo::Struct>()) {
            assert(false);
        }

        if (expected.is<TypeInfo::Primitive>()) {
            const TypeInfo::Primitive& expected_primitive = expected.get<TypeInfo::Primitive>();
            if (type->is<TypeInfo::Primitive>()) {
                const TypeInfo::Primitive& current_primitive = type->get<TypeInfo::Primitive>();

                return primitive_cast(
                    spv,
                    current_primitive.primitive,
                    expected_primitive.primitive,
                    expected.id,
                    loaded_id
                );
            } else if (type->is<TypeInfo::Vector>()) {
                assert(false);
            }
        } else if (expected.is<TypeInfo::Vector>()) {
            const TypeInfo::Vector& expected_vector = expected.get<TypeInfo::Vector>();
            if (type->is<TypeInfo::Vector>()) {
                const TypeInfo::Vector& current_vector = type->get<TypeInfo::Vector>();
                if (current_vector.size == expected_vector.size &&
                    current_vector.element->name == expected_vector.element->name) {
                    return loaded_id;
                }

                if (current_vector.size == expected_vector.size) {
                    TypeInfo::BuiltinPrimitive underlying_primitive =
                        current_vector.element->get_underlying_primitive().primitive;

                    TypeInfo::BuiltinPrimitive expected_element =
                        expected_vector.element->get_underlying_primitive().primitive;

                    return primitive_cast(
                        spv,
                        underlying_primitive,
                        expected_element,
                        expected.id,
                        loaded_id
                    );
                } else {
                    assert(false); // Vector size mismatch
                }

            } else if (type->is<TypeInfo::Primitive>()) {
                TypeInfo::BuiltinPrimitive expected_primitive =
                    expected_vector.element->get_underlying_primitive().primitive;
                TypeInfo::BuiltinPrimitive current_primitive =
                    type->get<TypeInfo::Primitive>().primitive;

                uint32_t converted_id = primitive_cast(
                    spv,
                    current_primitive,
                    expected_primitive,
                    expected.id,
                    loaded_id
                );

                std::vector<uint32_t> elements(expected_vector.size, converted_id);

                return spv.CompositeConstructNew(expected.id, elements.data(), elements.size());
            }
        }

        return 0;
    }

    void store(spv_binary::BinaryContainer& spv, uint32_t value) const {
        if (std::holds_alternative<uint32_t>(data)) {
            assert(false); // Cannot store to a raw ID, this should not happen
        } else if (std::holds_alternative<VariableInstance>(data)) {
            const VariableInstance& var = std::get<VariableInstance>(data);
            if (var.storage_class) {
                spv.Store(var.id, value);
            } else {
                assert(false); // Cannot store to a variable without storage class
            }
        } else if (std::holds_alternative<VectorSwizzle>(data)) {
            const VectorSwizzle& swizzle = std::get<VectorSwizzle>(data);
            Ref<ExprResult> inner = swizzle.vector;
            const TypeInfo::Vector& vector_type = inner->type->get<TypeInfo::Vector>();
            for (uint32_t i = 0; i < swizzle.component_count; i++) {
                uint32_t component_id = swizzle.component_constants[i];

                uint32_t access = spv.AccessChainNew(
                    vector_type.element->get_pointer_type(inner->get_storage_class()),
                    std::get<VariableInstance>(inner->data).id,
                    &component_id,
                    1
                );

                uint32_t component_value = spv.CompositeExtractNew(
                    inner->type->get<TypeInfo::Vector>().element->id,
                    value,
                    &i,
                    1
                );

                spv.Store(access, component_value);
            }
        }
    }

    spv::StorageClass get_storage_class() {
        if (std::holds_alternative<VariableInstance>(data)) {
            const VariableInstance& var = std::get<VariableInstance>(data);
            if (var.storage_class.has_value()) {
                return var.storage_class.value();
            }
        } else if (std::holds_alternative<VectorSwizzle>(data)) {
            return std::get<VectorSwizzle>(data).vector->get_storage_class();
        }
        assert(false); // Should not reach here
    }
};
