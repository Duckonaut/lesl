#pragma once

#include "arena.hpp"
#include "ref_container.hpp"
#include "repr.hpp"
#include "repr_walker.hpp"
#include "error_handler.hpp"
#include "builtin_functions.hpp"

#include <algorithm>
#include <array>
#include <variant>

struct Validator {
    CompilationArena& arena;
    ErrorHandler& error_handler;

    struct Variable {
        PoolStr name;
        Ref<TypeInfo> type;
        Opt<StorageClass> storage_class;

        Variable(PoolStr name, Ref<TypeInfo> type, Opt<StorageClass> storage_class)
            : name(name), type(type), storage_class(storage_class) {}
    };

    std::vector<std::vector<Variable>> variables;

    Validator(CompilationArena& arena, ErrorHandler& error_handler)
        : arena(arena), error_handler(error_handler) {}

    struct TypeInfoFiller : public ReprWalker {
        CompilationArena& arena;
        TypeInfoFiller(CompilationArena& arena) : arena(arena) {}

        using ReprWalker::visit;

        void visit(TypedIdentifier& typedIdentifier) override {
            ReprWalker::visit(typedIdentifier);

            typedIdentifier.type.resolved_type =
                create_or_get_info(arena, typedIdentifier.type);
        }

        void visit(Decl::Struct& struct_) override {
            ReprWalker::visit(struct_);

            struct_.resolved_type = create_or_get_info(
                arena,
                TypeRef{
                    struct_.name,
                    {},
                    {},
                }
            );
        }

        Ref<TypeInfo> create_or_get_info(CompilationArena& arena, const TypeRef& type) {
            TypeInfo info;
            bool filled = false;

            if (type.array_sizes.size() > 0) {
                TypeRef base_type = type;
                base_type.array_sizes.pop_back();
                Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

                int32_t size = type.array_sizes[type.array_sizes.size() - 1];

                info = TypeInfo::create_array(
                    arena.string_pool,
                    underlying_info,
                    size != -1,
                    size == -1 ? 0 : size
                );
                filled = true;
            } else {
                // search structs
                for (auto decl : arena.decls) {
                    if (decl->is<Decl::Struct>()) {
                        if (decl->get<Decl::Struct>().name.name == type.name.name) {
                            std::vector<TypeInfo::Struct::Member> members;
                            for (auto& member : decl->get<Decl::Struct>().members) {
                                members.push_back({ member.name.name,
                                                    create_or_get_info(arena, member.type) });
                            }

                            info = TypeInfo::create_struct(type.name.name, members);
                            filled = true;
                            break;
                        }
                    }
                }
                if (!filled) {
                    // not a struct or array, try builtins
                    std::string name = type.name.name.to_string();
                    bool is_vector = false;
                    bool is_matrix = false;
                    uint32_t vector_size = 0;
                    uint32_t matrix_columns = 0;
                    uint32_t matrix_rows = 0;

                    // extract matrix/vector sizes
                    if (std::isdigit(name[name.size() - 1])) {
                        if (name.size() >= 3 && std::isdigit(name[name.size() - 3]) &&
                            name[name.size() - 2] == 'x') {
                            matrix_columns = name[name.size() - 1] - '0';
                            matrix_rows = name[name.size() - 3] - '0';

                            is_matrix = true;

                            // base type
                            name =
                                name.substr(0, name.size() - 3) + std::to_string(matrix_rows);

                        } else {
                            vector_size = name[name.size() - 1] - '0';
                            is_vector = true;

                            name = name.substr(0, name.size() - 1);
                        }
                    }

                    if (is_vector) {
                        TypeRef base_type = TypeRef{
                            Identifier{ arena.string_pool.add(name), type.name.location },
                            {},
                            {},
                        };

                        Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

                        info = TypeInfo::create_vector(
                            arena.string_pool,
                            underlying_info,
                            vector_size
                        );
                        filled = true;
                    } else if (is_matrix) {
                        TypeRef base_type = TypeRef{
                            Identifier{ arena.string_pool.add(name), type.name.location },
                            {},
                            {},
                        };

                        Ref<TypeInfo> underlying_info = create_or_get_info(arena, base_type);

                        info = TypeInfo::create_matrix(
                            arena.string_pool,
                            underlying_info,
                            matrix_columns
                        );
                        filled = true;
                    } else {
                        TypeInfo::BuiltinPrimitive primitive = TypeInfo::BuiltinPrimitive::Void;
                        if (name == "void") {
                            primitive = TypeInfo::BuiltinPrimitive::Void;
                        } else if (name == "bool") {
                            primitive = TypeInfo::BuiltinPrimitive::Bool;
                        } else if (name == "int") {
                            primitive = TypeInfo::BuiltinPrimitive::Int;
                        } else if (name == "uint") {
                            primitive = TypeInfo::BuiltinPrimitive::Uint;
                        } else if (name == "float") {
                            primitive = TypeInfo::BuiltinPrimitive::Float;
                        }

                        info = TypeInfo::create_primitive(arena.string_pool, primitive);
                        filled = true;
                    }
                }
            }

            assert(filled);

            for (const Ref<TypeInfo>& t : arena.types) {
                if (*t == info) {
                    return t;
                }
            }

            Ref<TypeInfo> new_info = arena.alloc(std::move(info));
            return new_info;
        }
    };

    void open_scope() {
        variables.push_back({});
    }

    void close_scope() {
        variables.pop_back();
    }

    void
    add_variable(const PoolStr& name, Ref<TypeInfo> type, Opt<StorageClass> storage_class) {
        variables.back().push_back(Variable(name, type, storage_class));
    }

    Opt<Variable> find_variable(const PoolStr& name) {
        for (int i = variables.size() - 1; i >= 0; i--) {
            for (const Variable& var : variables[i]) {
                if (var.name == name) {
                    return var;
                }
            }
        }
        return std::nullopt;
    }

    Opt<std::variant<Ref<Decl>, BuiltinFunction>> find_function(const PoolStr& name) {
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                if (decl->get<Decl::Function>().name.name == name) {
                    return decl;
                }
            }
        }

        for (BuiltinFunction bf : builtin_functions) {
            if (name == bf.name) {
                return bf;
            }
        }

        return std::nullopt;
    }

    Opt<Ref<TypeInfo>> find_type_info(const char* name) {
        for (const Ref<TypeInfo>& type_info : arena.types) {
            if (type_info->name == name) {
                return type_info;
            }
        }
        return std::nullopt;
    }

    void validate() {
        // fill in type infos

        // add builtin neccessary types

        arena.alloc(
            TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Void)
        );
        arena.alloc(
            TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Int)
        );
        arena.alloc(
            TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Uint)
        );
        arena.alloc(
            TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Float)
        );
        arena.alloc(
            TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Bool)
        );

        // fill from refs
        TypeInfoFiller filler(arena);
        for (Ref<Decl> decl : arena.decls) {
            std::visit(
                [&filler](auto& decl) {
                    filler.visit(decl);
                },
                decl->data
            );
        }

        open_scope();

        // add builtin variables
        add_builtin_variables();

        // full validation
        for (Ref<Decl> decl : arena.decls) {
            if (decl->is<Decl::Function>()) {
                validate_function(decl->get<Decl::Function>());
            } else if (decl->is<Decl::Struct>()) {
                validate_struct(decl->get<Decl::Struct>());
            } else if (decl->is<Decl::Pipeline>()) {
                validate_pipeline(decl->get<Decl::Pipeline>());
            } else {
                assert(false);
            }
        }
        close_scope();
    }

    void add_builtin_variables() {
        add_variable(
            arena.string_pool.add("POSITION"),
            create_or_get_info_ref(TypeInfo::create_vector(
                arena.string_pool,
                create_or_get_info_ref(TypeInfo::create_primitive(
                    arena.string_pool,
                    TypeInfo::BuiltinPrimitive::Float
                )),
                4
            )),
            StorageClass::Output
        );
    }

    void validate_function(Decl::Function& f) {
        open_scope();
        for (TypedIdentifier& param : f.params) {
            validate_type(param.type);
            add_variable(
                param.name.name,
                param.type.resolved_type.value(),
                StorageClass::Input
            );
        }
        for (TypedIdentifier& ret : f.rets) {
            validate_type(ret.type);
            add_variable(ret.name.name, ret.type.resolved_type.value(), StorageClass::Output);
        }
        for (Ref<Stmt>& stmt : f.stmts) {
            validate_stmt(*stmt);
        }
        close_scope();
    }
    void validate_struct(Decl::Struct& s) {
        for (auto& member : s.members) {
            validate_type(member.type);
        }

        // check for runtime sized arrays that aren't the last member

        for (size_t i = 0; i < s.members.size(); i++) {
            if (s.members[i].type.array_sizes.size() > 0 &&
                s.members[i].type.array_sizes[s.members[i].type.array_sizes.size() - 1] == -1 &&
                i != s.members.size() - 1) {
                error_handler.error(ErrorType::InvalidArraySize, s.members[i].name.location);
            }
        }

        // check if runtime sized array exists and struct is not an interface

        bool has_runtime_array =
            std::any_of(s.members.begin(), s.members.end(), [](const auto& member) {
                return member.type.array_sizes.size() > 0 &&
                       member.type.array_sizes[member.type.array_sizes.size() - 1] == -1;
            });

        if (has_runtime_array && !s.is_interface) {
            error_handler
                .error(ErrorType::RuntimeSizedArrayInStruct, s.name.name, s.name.location);
        }

        // check for duplicate members
        for (size_t i = 0; i < s.members.size(); i++) {
            for (size_t j = i + 1; j < s.members.size(); j++) {
                if (s.members[i].name.name == s.members[j].name.name) {
                    error_handler.error(
                        ErrorType::StructMemberRedefinition,
                        s.members[i].name.location
                    );
                }
            }
        }
    }
    void validate_pipeline(Decl::Pipeline& p) {
        constexpr std::array<const char*, 2> necessary_params = {
            "Vertex",
            "Fragment",
        };

        for (const char* necessary_param : necessary_params) {
            bool found = false;
            for (const PipelineParameter& param : p.params) {
                if (param.name.name == necessary_param) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                error_handler.error(
                    ErrorType::MissingPipelineParameter,
                    arena.string_pool.add(necessary_param),
                    p.name.location
                );
            }
        }
    }
    void validate_stmt(Stmt& stmt) {
        if (stmt.is<Stmt::Return>()) {
            validate_return(stmt.get<Stmt::Return>());
        } else if (stmt.is<Stmt::Var>()) {
            validate_var(stmt.get<Stmt::Var>());
        } else if (stmt.is<Stmt::ExprStmt>()) {
            validate_expr(*stmt.get<Stmt::ExprStmt>().expr);
        } else {
            assert(false);
        }
    }
    void validate_return(Stmt::Return&) {}
    void validate_var(Stmt::Var& v) {
        validate_type(v.typedIdentifier.type);

        add_variable(
            v.typedIdentifier.name.name,
            v.typedIdentifier.type.resolved_type.value(),
            StorageClass::Function
        );

        if (v.expr) {
            validate_expr(*v.expr.value());
        }
    }
    void validate_type(TypeRef& type) {
        // check if is image sampler

        if (type.name.name == "sampler2D") {
            type.resolved_type = create_or_get_info_ref(TypeInfo::create_image_sampler(
                arena.string_pool
            ));
            return;
        }

        // validate array sizes
        uint32_t array_depth_total = type.array_sizes.size();
        for (uint32_t i = 0; i < array_depth_total; i++) {
            if (type.array_sizes[i] == 0) {
                error_handler.error(ErrorType::InvalidArraySize, type.name.location);
            }
            if (type.array_sizes[i] == -1 && i != array_depth_total - 1) {
                error_handler.error(ErrorType::InvalidArraySize, type.name.location);
            }
        }

        // validate base type
        // search structs
        for (auto decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                if (decl->get<Decl::Struct>().name.name == type.name.name) {
                    return;
                }
            }
        }
        // search builtins

        std::string name = type.name.name.to_string();
        bool is_vector = false;
        bool is_matrix = false;

        // extract matrix/vector sizes
        if (std::isdigit(name[name.size() - 1])) {
            if (name.size() >= 3 && std::isdigit(name[name.size() - 3]) &&
                name[name.size() - 2] == 'x') {
                // it's probably a matrix
                uint32_t rows = name[name.size() - 1] - '0';
                uint32_t columns = name[name.size() - 3] - '0';

                if (columns < 2 || columns > 4 || rows < 2 || rows > 4) {
                    error_handler.error(ErrorType::InvalidMatrixSize, type.name.location);
                }

                is_matrix = true;

                name = name.substr(0, name.size() - 3);

            } else {
                // it's probably a vector
                uint32_t size = name[name.size() - 1] - '0';

                if (size < 2 || size > 4) {
                    error_handler.error(ErrorType::InvalidVectorSize, type.name.location);
                }

                is_vector = true;

                name = name.substr(0, name.size() - 1);
            }
        }

        if (is_matrix) {
            if (name == "float") {
                return;
            } else {
                error_handler.error(
                    ErrorType::InvalidCompoundBaseType,
                    type.name.name,
                    type.name.location
                );

                return;
            }
        } else if (is_vector) {
            if (name == "float" || name == "int" || name == "uint") {
                return;
            } else {
                error_handler.error(
                    ErrorType::InvalidCompoundBaseType,
                    type.name.name,
                    type.name.location
                );

                return;
            }
        } else {
            if (name == "void" || name == "bool" || name == "int" || name == "uint" ||
                name == "float") {
                return;
            }
        }

        error_handler.error(ErrorType::UnknownType, type.name.name, type.name.location);
    }

    struct ExprValidationResult {
        Opt<Ref<TypeInfo>> type;
    };

    ExprValidationResult validate_expr(
        Expr& expr,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool assignable = false
    ) {
        if (expr.is<Expr::Binary>()) {
            return validate_binary(expr.get<Expr::Binary>(), expected_type, assignable);
        } else if (expr.is<Expr::Unary>()) {
            return validate_unary(expr.get<Expr::Unary>(), expected_type, assignable);
        } else if (expr.is<Expr::VariableAccess>()) {
            return validate_variable_access(
                expr.get<Expr::VariableAccess>(),
                expected_type,
                assignable
            );
        } else if (expr.is<Expr::FieldAccess>()) {
            return validate_field_access(
                expr.get<Expr::FieldAccess>(),
                expected_type,
                assignable
            );
        } else if (expr.is<Expr::NumberLiteral>()) {
            return validate_number_literal(
                expr.get<Expr::NumberLiteral>(),
                expected_type,
                assignable
            );
        } else if (expr.is<Expr::Call>()) {
            return validate_call(expr.get<Expr::Call>(), expected_type, assignable);
        } else if (expr.is<Expr::ListAccess>()) {
            return validate_list_access(
                expr.get<Expr::ListAccess>(),
                expected_type,
                assignable
            );
        } else {
            assert(false);
        }
    }

    ExprValidationResult validate_binary(
        Expr::Binary& binary,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool assignable = false
    ) {
        bool will_be_assigned =
            binary.op == Expr::BinaryOp::Assign || binary.op == Expr::BinaryOp::AddAssign ||
            binary.op == Expr::BinaryOp::SubAssign || binary.op == Expr::BinaryOp::MulAssign ||
            binary.op == Expr::BinaryOp::DivAssign || binary.op == Expr::BinaryOp::ModAssign;

        ExprValidationResult left = validate_expr(*binary.lhs, expected_type, will_be_assigned);
        ExprValidationResult right = validate_expr(*binary.rhs, expected_type);

        if (!left.type || !right.type) {
            return { std::nullopt };
        }

        const TypeInfo& left_type = **left.type;
        const TypeInfo& right_type = **right.type;

        if (assignable) {
            error_handler.error(ErrorType::InvalidAssignment, binary.lhs->get_location());
        }

        switch (binary.op) {
            case Expr::BinaryOp::Assign:
                if (*left.type != *right.type) {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        binary.lhs->get_location()
                    );
                }

                return { left.type };
            case Expr::BinaryOp::Add:
            case Expr::BinaryOp::Sub:
            case Expr::BinaryOp::Div:
            case Expr::BinaryOp::Mod:
            case Expr::BinaryOp::AddAssign:
            case Expr::BinaryOp::SubAssign:
            case Expr::BinaryOp::DivAssign:
            case Expr::BinaryOp::ModAssign: {
                if (left_type.is<TypeInfo::Array>() || left_type.is<TypeInfo::Matrix>() ||
                    left_type.is<TypeInfo::Struct>() || right_type.is<TypeInfo::Array>() ||
                    right_type.is<TypeInfo::Matrix>() || right_type.is<TypeInfo::Struct>()) {
                    error_handler.error(
                        ErrorType::InvalidCompositeArithmetic,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Primitive>() && right_type.is<TypeInfo::Vector>()) {
                    // disallow scalar-vector arithmetic (vector-scalar is allowed)
                    error_handler.error(
                        ErrorType::InvalidCompositeArithmetic,
                        binary.lhs->get_location()
                    );
                }

                const TypeInfo::BuiltinPrimitive left_primitive =
                    left_type.get_underlying_primitive().primitive;
                const TypeInfo::BuiltinPrimitive right_primitive =
                    right_type.get_underlying_primitive().primitive;

                // only allow arithmetic for arithmetic-compatible primitives
                if (!(left_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Uint) ||
                    !(right_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Uint)) {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Vector>() && right_type.is<TypeInfo::Vector>()) {
                    // only allow vector arithmetic for vectors of the same size
                    // and with arithmetic-compatible primitives
                    if (left_type.get<TypeInfo::Vector>().size !=
                        right_type.get<TypeInfo::Vector>().size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }
                }

                return { left.type };
            }
            case Expr::BinaryOp::Mul:
            case Expr::BinaryOp::MulAssign: {
                // multiplication is allowed for:
                // - primitive * primitive
                // - vector * primitive
                // - matrix * primitive
                // - vector * vector (only if they are the same size)
                // - matrix * vector (only if they are compatible)
                // - matrix * matrix (only if they are compatible)

                if (left_type.is<TypeInfo::Array>() || left_type.is<TypeInfo::Struct>() ||
                    right_type.is<TypeInfo::Array>() || right_type.is<TypeInfo::Struct>()) {
                    error_handler.error(
                        ErrorType::InvalidCompositeArithmetic,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Primitive>() && right_type.is<TypeInfo::Vector>()) {
                    // disallow scalar-vector arithmetic (vector-scalar is allowed)
                    error_handler.error(
                        ErrorType::InvalidCompositeArithmetic,
                        binary.lhs->get_location()
                    );
                }

                const TypeInfo::BuiltinPrimitive left_primitive =
                    left_type.get_underlying_primitive().primitive;
                const TypeInfo::BuiltinPrimitive right_primitive =
                    right_type.get_underlying_primitive().primitive;

                // only allow multiplication for arithmetic-compatible primitives
                if (!(left_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Uint) ||
                    !(right_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Uint)) {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Vector>() && right_type.is<TypeInfo::Vector>()) {
                    // only allow vector multiplication for vectors of the same size
                    // and with arithmetic-compatible primitives
                    if (left_type.get<TypeInfo::Vector>().size !=
                        right_type.get<TypeInfo::Vector>().size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }
                } else if (left_type.is<TypeInfo::Matrix>() &&
                           right_type.is<TypeInfo::Matrix>()) {
                    // only allow matrix multiplication for matrices with compatible sizes
                    int left_columns = left_type.get<TypeInfo::Matrix>().columns;
                    const TypeInfo::Vector& right_vector_element =
                        right_type.get<TypeInfo::Matrix>()
                            .vector_element->get<TypeInfo::Vector>();
                    int right_rows = right_vector_element.size;

                    if (left_columns != right_rows) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }

                    int result_columns = left_columns;

                    // the result type can be different from either left or right,
                    // so we need to create a new type info for it

                    TypeInfo result_type = TypeInfo::create_matrix(
                        arena.string_pool,
                        left_type.get<TypeInfo::Matrix>().vector_element,
                        result_columns
                    );

                    return { create_or_get_info_ref(std::move(result_type)) };
                } else if (left_type.is<TypeInfo::Matrix>() &&
                           right_type.is<TypeInfo::Vector>()) {
                    // only allow matrix-vector multiplication for matrices with compatible
                    // sizes

                    int left_columns = left_type.get<TypeInfo::Matrix>().columns;
                    int right_size = right_type.get<TypeInfo::Vector>().size;

                    if (left_columns != right_size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }

                    // the result type is the same as the vector element type of the matrix

                    return { left_type.get<TypeInfo::Matrix>().vector_element };
                } else if (left_type.is<TypeInfo::Vector>() &&
                           right_type.is<TypeInfo::Matrix>()) {
                    // only allow vector-matrix multiplication for vectors with compatible
                    // sizes

                    int left_size = left_type.get<TypeInfo::Vector>().size;
                    int right_rows = right_type.get<TypeInfo::Matrix>().vector_element->size;
                    int right_columns = right_type.get<TypeInfo::Matrix>().columns;

                    if (left_size != right_rows) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }

                    // the result type is a vector the same size as the number of matrix columns
                    // this type can be different from either left or right

                    TypeInfo result_type = TypeInfo::create_vector(
                        arena.string_pool,
                        left_type.get<TypeInfo::Vector>().element,
                        right_columns
                    );

                    return { create_or_get_info_ref(std::move(result_type)) };
                } else if (left_type.is<TypeInfo::Matrix>() &&
                           right_type.is<TypeInfo::Primitive>()) {
                    // matrix * primitive is allowed, the result type is the same as the matrix
                    return { left.type };
                }

                return { left.type };

                break;
            }
            case Expr::BinaryOp::And:
            case Expr::BinaryOp::Or: {
                // only allow logical operations on boolean type primtives or vectors of
                // booleans
                if (left_type.is<TypeInfo::Array>() || left_type.is<TypeInfo::Matrix>() ||
                    left_type.is<TypeInfo::Struct>() || right_type.is<TypeInfo::Array>() ||
                    right_type.is<TypeInfo::Matrix>() || right_type.is<TypeInfo::Struct>()) {
                    error_handler.error(
                        ErrorType::InvalidLogicalType,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo::BuiltinPrimitive left_primitive =
                    left_type.get_underlying_primitive().primitive;
                TypeInfo::BuiltinPrimitive right_primitive =
                    right_type.get_underlying_primitive().primitive;

                if (left_primitive != TypeInfo::BuiltinPrimitive::Bool ||
                    right_primitive != TypeInfo::BuiltinPrimitive::Bool) {
                    error_handler.error(
                        ErrorType::InvalidLogicalType,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Vector>() && right_type.is<TypeInfo::Vector>()) {
                    // only allow vector logical operations for vectors of the same size
                    if (left_type.get<TypeInfo::Vector>().size !=
                        right_type.get<TypeInfo::Vector>().size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );

                        TypeInfo result_type = TypeInfo::create_vector(
                            arena.string_pool,
                            create_or_get_info_ref(TypeInfo::create_primitive(
                                arena.string_pool,
                                TypeInfo::BuiltinPrimitive::Bool
                            )),
                            left_type.get<TypeInfo::Vector>().size
                        );

                        return { create_or_get_info_ref(std::move(result_type)) };
                    }

                } else if (left_type.is<TypeInfo::Primitive>() &&
                           right_type.is<TypeInfo::Primitive>()) {
                    // it's fine
                } else {
                    error_handler.error(
                        ErrorType::InvalidLogicalType,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo result_type = TypeInfo::create_primitive(
                    arena.string_pool,
                    TypeInfo::BuiltinPrimitive::Bool
                );

                return { create_or_get_info_ref(std::move(result_type)) };
            }
            case Expr::BinaryOp::Equal:
            case Expr::BinaryOp::NotEqual: {
                // equality and inequality can be checked for:
                // - primitives
                // - vectors of the same size and primitive type
                // as long as their underlying types are the same

                if (left_type.is<TypeInfo::Array>() || left_type.is<TypeInfo::Matrix>() ||
                    left_type.is<TypeInfo::Struct>() || right_type.is<TypeInfo::Array>() ||
                    right_type.is<TypeInfo::Matrix>() || right_type.is<TypeInfo::Struct>()) {
                    error_handler.error(
                        ErrorType::InvalidEqualityComparison,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo::BuiltinPrimitive left_primitive =
                    left_type.get_underlying_primitive().primitive;
                TypeInfo::BuiltinPrimitive right_primitive =
                    right_type.get_underlying_primitive().primitive;

                if (left_primitive != right_primitive) {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Vector>() && right_type.is<TypeInfo::Vector>()) {
                    // only allow vector equality for vectors of the same size
                    if (left_type.get<TypeInfo::Vector>().size !=
                        right_type.get<TypeInfo::Vector>().size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }

                    TypeInfo result_type = TypeInfo::create_vector(
                        arena.string_pool,
                        create_or_get_info_ref(TypeInfo::create_primitive(
                            arena.string_pool,
                            TypeInfo::BuiltinPrimitive::Bool
                        )),
                        left_type.get<TypeInfo::Vector>().size
                    );

                    return { create_or_get_info_ref(std::move(result_type)) };
                } else if (left_type.is<TypeInfo::Primitive>() &&
                           right_type.is<TypeInfo::Primitive>()) {
                    // it's fine
                } else {
                    error_handler.error(
                        ErrorType::InvalidEqualityComparison,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo result_type = TypeInfo::create_primitive(
                    arena.string_pool,
                    TypeInfo::BuiltinPrimitive::Bool
                );

                return { create_or_get_info_ref(std::move(result_type)) };
            }
            case Expr::BinaryOp::Less:
            case Expr::BinaryOp::LessEqual:
            case Expr::BinaryOp::Greater:
            case Expr::BinaryOp::GreaterEqual: {
                // relational operators can be used on:
                // - primitives
                // - vectors of the same size and primitive type
                // as long as their underlying types are the same and
                // the primitive type is arithmetic-compatible

                if (left_type.is<TypeInfo::Array>() || left_type.is<TypeInfo::Matrix>() ||
                    left_type.is<TypeInfo::Struct>() || right_type.is<TypeInfo::Array>() ||
                    right_type.is<TypeInfo::Matrix>() || right_type.is<TypeInfo::Struct>()) {
                    error_handler.error(
                        ErrorType::InvalidRelationalComparison,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo::BuiltinPrimitive left_primitive =
                    left_type.get_underlying_primitive().primitive;
                TypeInfo::BuiltinPrimitive right_primitive =
                    right_type.get_underlying_primitive().primitive;

                if (!(left_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      left_primitive == TypeInfo::BuiltinPrimitive::Uint) ||
                    !(right_primitive == TypeInfo::BuiltinPrimitive::Float ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Int ||
                      right_primitive == TypeInfo::BuiltinPrimitive::Uint)) {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        binary.lhs->get_location()
                    );
                }

                if (left_type.is<TypeInfo::Vector>() && right_type.is<TypeInfo::Vector>()) {
                    // only allow vector relational operations for vectors of the same size
                    if (left_type.get<TypeInfo::Vector>().size !=
                        right_type.get<TypeInfo::Vector>().size) {
                        error_handler.error(
                            ErrorType::IncompatibleTypes,
                            binary.lhs->get_location()
                        );
                    }

                    TypeInfo result_type = TypeInfo::create_vector(
                        arena.string_pool,
                        create_or_get_info_ref(TypeInfo::create_primitive(
                            arena.string_pool,
                            TypeInfo::BuiltinPrimitive::Bool
                        )),
                        left_type.get<TypeInfo::Vector>().size
                    );

                    return { create_or_get_info_ref(std::move(result_type)) };
                } else if (left_type.is<TypeInfo::Primitive>() &&
                           right_type.is<TypeInfo::Primitive>()) {
                    // it's fine
                } else {
                    error_handler.error(
                        ErrorType::InvalidRelationalComparison,
                        binary.lhs->get_location()
                    );
                }

                TypeInfo result_type = TypeInfo::create_primitive(
                    arena.string_pool,
                    TypeInfo::BuiltinPrimitive::Bool
                );
                return { create_or_get_info_ref(std::move(result_type)) };
            }
        }
    }

    ExprValidationResult validate_unary(
        Expr::Unary& unary,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool assignable = false
    ) {
        ExprValidationResult inner = validate_expr(*unary.expr, expected_type);

        if (assignable) {
            error_handler.error(ErrorType::InvalidAssignment, unary.expr->get_location());
        }

        if (!inner.type) {
            return { std::nullopt };
        }

        const TypeInfo& type = **inner.type;

        if (unary.op == Expr::UnaryOp::Neg) {
            if (type.is<TypeInfo::Array>() || type.is<TypeInfo::Matrix>() ||
                type.is<TypeInfo::Struct>()) {
                error_handler.error(
                    ErrorType::InvalidCompositeArithmetic,
                    unary.expr->get_location()
                );
            }

            TypeInfo::BuiltinPrimitive primitive = type.get_underlying_primitive().primitive;

            if (!(primitive == TypeInfo::BuiltinPrimitive::Float ||
                  primitive == TypeInfo::BuiltinPrimitive::Int ||
                  primitive == TypeInfo::BuiltinPrimitive::Uint)) {
                error_handler.error(ErrorType::IncompatibleTypes, unary.expr->get_location());
            }
        } else if (unary.op == Expr::UnaryOp::Not) {
            if (type.is<TypeInfo::Array>() || type.is<TypeInfo::Matrix>() ||
                type.is<TypeInfo::Struct>()) {
                error_handler.error(ErrorType::InvalidLogicalType, unary.expr->get_location());
            }
            TypeInfo::BuiltinPrimitive primitive = type.get_underlying_primitive().primitive;
            if (primitive != TypeInfo::BuiltinPrimitive::Bool) {
                error_handler.error(ErrorType::InvalidLogicalType, unary.expr->get_location());
            }
        } else {
            assert(false);
        }

        return { inner.type };
    }

    bool
    is_primitive_convertible(TypeInfo::BuiltinPrimitive from, TypeInfo::BuiltinPrimitive to) {
        if (from == to) {
            return true;
        }

        // allow implicit conversions between int, uint and float
        if ((from == TypeInfo::BuiltinPrimitive::Int &&
             (to == TypeInfo::BuiltinPrimitive::Uint || to == TypeInfo::BuiltinPrimitive::Float)
            ) ||
            (from == TypeInfo::BuiltinPrimitive::Uint &&
             (to == TypeInfo::BuiltinPrimitive::Int || to == TypeInfo::BuiltinPrimitive::Float)
            ) ||
            (from == TypeInfo::BuiltinPrimitive::Float &&
             (to == TypeInfo::BuiltinPrimitive::Int || to == TypeInfo::BuiltinPrimitive::Uint)
            )) {
            return true;
        }

        return false;
    }

    bool is_type_convertible(Ref<TypeInfo> from, Ref<TypeInfo> to) {
        if (*from == *to) {
            return true;
        }
        if (from->is<TypeInfo::Primitive>() && to->is<TypeInfo::Primitive>()) {
            TypeInfo::BuiltinPrimitive from_primitive =
                from->get_underlying_primitive().primitive;
            TypeInfo::BuiltinPrimitive to_primitive = to->get_underlying_primitive().primitive;

            return is_primitive_convertible(from_primitive, to_primitive);
        } else if (from->is<TypeInfo::Vector>() && to->is<TypeInfo::Vector>()) {
            // allow implicit conversions between vectors of the same size and primitive types
            // like the previous case
            if (from->get<TypeInfo::Vector>().size == to->get<TypeInfo::Vector>().size) {
                TypeInfo::BuiltinPrimitive from_primitive =
                    from->get<TypeInfo::Vector>().element->get_underlying_primitive().primitive;
                TypeInfo::BuiltinPrimitive to_primitive =
                    to->get<TypeInfo::Vector>().element->get_underlying_primitive().primitive;

                return is_primitive_convertible(from_primitive, to_primitive);
            }
        }

        return false;
    }

    ExprValidationResult validate_variable_access(
        Expr::VariableAccess& variable,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool assignable = false
    ) {
        Opt<Variable> var = find_variable(variable.name.name);
        if (!var) {
            error_handler
                .error(ErrorType::UnknownVariable, variable.name.name, variable.name.location);
            return { std::nullopt };
        }
        if (expected_type.has_value()) {
            if (var->type != expected_type.value()) {
                // check if convertible

                if (!is_type_convertible(var->type, expected_type.value())) {
                    error_handler.error(ErrorType::IncompatibleTypes, variable.name.location);
                    return { std::nullopt };
                }
            }
        }

        if (assignable) {
            if (var->storage_class == StorageClass::Input ||
                var->storage_class == StorageClass::Uniform ||
                var->storage_class == StorageClass::StorageBuffer) {
                error_handler.error(
                    ErrorType::InvalidVariableAssignment,
                    variable.name.name,
                    variable.name.location
                );
                return { std::nullopt };
            }
        }
        return { var->type };
    }

    bool is_valid_vector_swizzle(const PoolStr& name, uint32_t vector_size, bool assignable) {
        if (name.size() > 4) {
            return false; // swizzle names can only be up to 4 characters long
        }
        if (name.size() == 0) {
            return false; // empty swizzle name is invalid
        }

        std::string s = name.to_string();

        bool use_xyzw = false;
        bool use_rgba = false;

        for (char c : s) {
            if (c == 'x' || c == 'y' || c == 'z' || c == 'w') {
                use_xyzw = true;
            } else if (c == 'r' || c == 'g' || c == 'b' || c == 'a') {
                use_rgba = true;
            } else {
                return false; // invalid character
            }
        }

        if (use_xyzw && use_rgba) {
            return false; // can't use both xyzw and rgba
        }

        for (char c : s) {
            int index = 0;
            if (c == 'x' || c == 'r') {
                index = 0;
            } else if (c == 'y' || c == 'g') {
                index = 1;
            } else if (c == 'z' || c == 'b') {
                index = 2;
            } else if (c == 'w' || c == 'a') {
                index = 3;
            }

            if (index >= vector_size) {
                return false; // index out of bounds for the vector size
            }
        }

        if (assignable) {
            int component_uses[4] = { 0, 0, 0, 0 }; // x, y, z, w

            for (char c : s) {
                if (c == 'x' || c == 'r') {
                    component_uses[0]++;
                } else if (c == 'y' || c == 'g') {
                    component_uses[1]++;
                } else if (c == 'z' || c == 'b') {
                    component_uses[2]++;
                } else if (c == 'w' || c == 'a') {
                    component_uses[3]++;
                }
            }

            // for an assignable swizzle, each component can be used at most once
            for (int i = 0; i < 4; i++) {
                if (component_uses[i] > 1) {
                    return false; // component used more than once
                }
            }
        }

        return true;
    }

    ExprValidationResult validate_field_access(
        Expr::FieldAccess& field_access,
        Opt<Ref<TypeInfo>> = std::nullopt,
        bool assignable = false
    ) {
        ExprValidationResult base = validate_expr(*field_access.object);

        if (!base.type) {
            return { std::nullopt };
        }

        const TypeInfo& base_type = **base.type;

        if (base_type.is<TypeInfo::Struct>()) {
            const TypeInfo::Struct& struct_type = base_type.get<TypeInfo::Struct>();

            auto it = std::find_if(
                struct_type.members.begin(),
                struct_type.members.end(),
                [&field_access](const TypeInfo::Struct::Member& member) {
                    return member.name == field_access.field.name;
                }
            );

            if (it == struct_type.members.end()) {
                error_handler.error(
                    ErrorType::InvalidAccess,
                    field_access.field.name,
                    field_access.field.location
                );
                return { std::nullopt };
            }

            return { it->type };
        } else if (base_type.is<TypeInfo::Vector>()) {
            const TypeInfo::Vector& vector_type = base_type.get<TypeInfo::Vector>();

            if (!is_valid_vector_swizzle(
                    field_access.field.name,
                    vector_type.size,
                    assignable
                )) {
                error_handler.error(
                    ErrorType::InvalidVectorSwizzle,
                    field_access.field.name,
                    field_access.field.location
                );
                return { std::nullopt };
            }
            // swizzle is valid, we can return the type of the swizzle
            if (field_access.field.name.size() == 1) {
                // if the vector is of size 1, the swizzle is just the element type
                return { vector_type.element };
            } else {
                TypeInfo result_type = TypeInfo::create_vector(
                    arena.string_pool,
                    base_type.get<TypeInfo::Vector>().element,
                    field_access.field.name.size()
                );
                return { create_or_get_info_ref(std::move(result_type)) };
            }
        } else if (base_type.is<TypeInfo::Matrix>()) {
            // matrix access has no swizzles, instead components are
            // accessed by names like "x0", "y1", "z2", "w3"

            const TypeInfo::Matrix& matrix_type = base_type.get<TypeInfo::Matrix>();
            std::string name = field_access.field.name.to_string();

            const TypeInfo::Vector& vector_element =
                matrix_type.vector_element->get<TypeInfo::Vector>();

            char end_char = 'x' + (vector_element.size - 1);

            if (name.size() != 2 || name[0] < 'x' || name[0] > end_char || name[1] < '0' ||
                name[1] >= '0' + matrix_type.columns) {
                error_handler.error(
                    ErrorType::InvalidMatrixAccess,
                    field_access.field.name,
                    field_access.field.location
                );
                return { std::nullopt };
            }

            return { vector_element.element };
        } else {
            error_handler.error(
                ErrorType::InvalidAccess,
                field_access.field.name,
                field_access.field.location
            );
            return { std::nullopt };
        }
    }

    ExprValidationResult validate_number_literal(
        Expr::NumberLiteral& number,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool assignable = false
    ) {
        if (assignable) {
            error_handler.error(ErrorType::InvalidAssignment, number.location);
            return { std::nullopt };
        }

        if (expected_type.has_value()) {
            const TypeInfo& expected = *expected_type.value();

            if (expected.is<TypeInfo::Primitive>()) {
                auto wanted_primitive = expected.get<TypeInfo::Primitive>().primitive;

                if (wanted_primitive == TypeInfo::BuiltinPrimitive::Int ||
                    wanted_primitive == TypeInfo::BuiltinPrimitive::Uint ||
                    wanted_primitive == TypeInfo::BuiltinPrimitive::Float) {
                    return { expected_type.value() };
                } else {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        number.location
                    );
                    return { std::nullopt };
                }
            } else if (expected.is<TypeInfo::Vector>()) {
                TypeInfo::Vector vec = expected.get<TypeInfo::Vector>();
                TypeInfo::BuiltinPrimitive element_primitive =
                    vec.element->get_underlying_primitive().primitive;

                if (element_primitive == TypeInfo::BuiltinPrimitive::Int ||
                    element_primitive == TypeInfo::BuiltinPrimitive::Uint ||
                    element_primitive == TypeInfo::BuiltinPrimitive::Float) {
                    return { expected_type.value() };
                } else {
                    error_handler.error(
                        ErrorType::IncompatibleTypes,
                        number.location
                    );
                    return { std::nullopt };
                }
            } else {
                error_handler.error(
                    ErrorType::IncompatibleTypes,
                    number.location
                );
                return { std::nullopt };
            }
        } else {
            // assume float by default
            TypeInfo type = TypeInfo::create_primitive(
                arena.string_pool,
                TypeInfo::BuiltinPrimitive::Float
            );

            return { create_or_get_info_ref(std::move(type)) };
        }
    }

    ExprValidationResult validate_call(
        Expr::Call& call,
        Opt<Ref<TypeInfo>> = std::nullopt,
        bool assignable = false
    ) {
        if (assignable) {
            error_handler.error(ErrorType::InvalidAssignment, call.name.location);
            return { std::nullopt };
        }

        auto opt_func = find_function(call.name.name);

        if (!opt_func) {
            error_handler.error(ErrorType::UnknownFunction, call.name.name, call.name.location);
            return { std::nullopt };
        }

        auto func = opt_func.value();

        return std::visit(
            overloaded{
                [this, &call](Ref<Decl>& decl) -> ExprValidationResult {
                    Decl::Function& f = decl->get<Decl::Function>();

                    if (call.args.size() != f.params.size()) {
                        error_handler.error(
                            ErrorType::BadCallArgumentCount,
                            call.name.location
                        );
                    } else {
                        for (int i = 0; i < f.params.size(); i++) {
                            Opt<Ref<TypeInfo>> expected =
                                find_type_info(f.params[i].type.name.name.c_str());
                            Opt<Ref<TypeInfo>> actual =
                                validate_expr(*call.args[i], expected).type;

                            if (expected != std::nullopt && actual != std::nullopt) {
                                if (!is_type_convertible(*actual, *expected)) {
                                    error_handler.error(
                                        ErrorType::BadCallArgument,
                                        f.params[i].name.name,
                                        call.args[i]->get_location()
                                    );
                                }
                            }
                        }
                    }

                    return { f.rets[0].type.resolved_type.value() };
                },
                [this, &call](BuiltinFunction& builtin) -> ExprValidationResult {
                    std::vector<Ref<TypeInfo>> arg_types;
                    Opt<Ref<TypeInfo>> type_hint = std::nullopt;

                    BuiltinInputKind bik = builtin.input_kind;
                    BuiltinOutputKind bok = builtin.output_kind;

                    for (auto& arg : call.args) {
                        ExprValidationResult evr = validate_expr(*arg, type_hint);

                        if (evr.type != std::nullopt) {
                            arg_types.push_back(*evr.type);
                        } else {
                            return { std::nullopt };
                        }
                    }

                    int vector_size = -1;
                    int static_input_set = -1;
                    Opt<TypeInfo::BuiltinPrimitive> chosen_primitive;

                    switch (bik) {
                        case BuiltinInputKind::Static: {
                            static_input_set = -1;
                            for (int k = 0; k < builtin.inputs.size(); k++) {
                                auto& inputs = builtin.inputs[k];
                                if (inputs.size() != call.args.size()) {
                                    continue;
                                }
                                bool invalid = false;
                                for (int i = 0; i < inputs.size(); i++) {
                                    Opt<Ref<TypeInfo>> expected = find_type_info(inputs[i]);
                                    Opt<Ref<TypeInfo>> actual =
                                        validate_expr(*call.args[i], expected).type;

                                    if (expected != std::nullopt && actual != std::nullopt) {
                                        if (!is_type_convertible(*actual, *expected)) {
                                            invalid = true;
                                            break;
                                        }
                                    }
                                }

                                if (!invalid) {
                                    static_input_set = k;
                                    break;
                                }
                            }

                            break;
                        }
                        case BuiltinInputKind::Packed: {
                            uint32_t components = builtin.required_packed_input;
                            TypeInfo::BuiltinPrimitive pack_primitive =
                                builtin.base_input_primitive;

                            uint32_t my_components = 0;
                            for (uint32_t i = 0; i < arg_types.size(); i++) {
                                Ref<TypeInfo> a = arg_types[i];

                                if (a->is<TypeInfo::Primitive>()) {
                                    TypeInfo::Primitive& p = a->get<TypeInfo::Primitive>();

                                    if (p.primitive != pack_primitive) {
                                        error_handler.error(
                                            ErrorType::BadCallArgument,
                                            call.args[i]->get_location()
                                        );
                                    }

                                    my_components += 1;
                                } else if (a->is<TypeInfo::Vector>()) {
                                    TypeInfo::Vector& v = a->get<TypeInfo::Vector>();
                                    TypeInfo::BuiltinPrimitive p =
                                        a->get_underlying_primitive().primitive;

                                    if (p != pack_primitive) {
                                        error_handler.error(
                                            ErrorType::BadPackedInputPrimitiveType,
                                            call.args[i]->get_location()
                                        );
                                    }

                                    my_components += v.size;
                                } else {
                                    error_handler.error(
                                        ErrorType::BadPackedInputType,
                                        call.args[i]->get_location()
                                    );
                                }
                            }

                            if (my_components != components) {
                                error_handler.error(
                                    ErrorType::BadPackedInput,
                                    call.args[0]->get_location()
                                );
                            }

                            chosen_primitive = pack_primitive;
                            break;
                        }
                        case BuiltinInputKind::Vectorized:
                            TypeInfo::BuiltinPrimitive base_primitive =
                                arg_types[0]->get_underlying_primitive().primitive;
                            if (arg_types[0]->is<TypeInfo::Primitive>()) {
                                vector_size = 1;
                            } else if (arg_types[0]->is<TypeInfo::Vector>()) {
                                vector_size = arg_types[0]->get<TypeInfo::Vector>().size;
                            } else {
                                error_handler.error(
                                    ErrorType::BadVectorInputType,
                                    arg_types[0]->name,
                                    call.args[0]->get_location()
                                );
                            }

                            bool accepted = false;
                            for (uint32_t i = 0; i < builtin.allowed_primitive_inputs.size();
                                 i++) {
                                if (builtin.allowed_primitive_inputs[i] == base_primitive) {
                                    accepted = true;
                                    break;
                                }
                            }

                            if (!accepted) {
                                error_handler.error(
                                    ErrorType::BadVectorPrimitive,
                                    arena.string_pool.add(
                                        TypeInfo::builtin_primitive_str(base_primitive)
                                    ),
                                    call.args[0]->get_location()
                                );
                            }

                            if (vector_size < builtin.min_vector_size ||
                                vector_size > builtin.max_vector_size) {
                                error_handler.error(
                                    ErrorType::BadVectorSize,
                                    call.args[0]->get_location()
                                );
                            }

                            if (arg_types.size() != 1) {
                                error_handler.error(
                                    ErrorType::BadCallArgumentCount,
                                    call.name.location
                                );
                            }

                            for (uint32_t i = 1; i < arg_types.size(); i++) {
                                Ref<TypeInfo>& t = arg_types[i];

                                if (t != arg_types[0]) {
                                    error_handler.error(
                                        ErrorType::BadVectorInputInconsistent,
                                        call.args[i]->get_location()
                                    );
                                }
                            }

                            chosen_primitive = base_primitive;

                            break;
                    }

                    switch (bok) {
                        case BuiltinOutputKind::Static:
                            // always the same
                            return { find_type_info(builtin.static_output) };
                        case BuiltinOutputKind::InheritedSingle:
                            // primitive of the first argument
                            return {
                                find_type_info(TypeInfo::builtin_primitive_str(*chosen_primitive
                                )),
                            };
                            break;
                        case BuiltinOutputKind::StaticVectorized:
                            // always same primitive, size of the first argument
                            if (bik == BuiltinInputKind::Vectorized) {
                                return { find_type_info(
                                    TypeInfo::builtin_primitive_str(builtin.static_output_base)
                                ) };
                            } else if (bik == BuiltinInputKind::Static) {
                                Ref<TypeInfo> first_arg_type = arg_types[0];

                                int vec_size = 1;
                                if (first_arg_type->is<TypeInfo::Vector>()) {
                                    vec_size = first_arg_type->get<TypeInfo::Vector>().size;
                                }

                                TypeInfo result_type = TypeInfo::create_vector(
                                    arena.string_pool,
                                    *find_type_info(
                                        TypeInfo::builtin_primitive_str(
                                            builtin.static_output_base
                                        )
                                    ),
                                    vec_size
                                );

                                return { create_or_get_info_ref(std::move(result_type)) };
                            }

                            assert(false);
                            break;

                        case BuiltinOutputKind::Inherited:
                            return { arg_types[0] };
                    }
                },
            },
            func
        );
    }

    ExprValidationResult validate_list_access(
        Expr::ListAccess& list_access,
        Opt<Ref<TypeInfo>> expected_type = std::nullopt,
        bool = false
    ) {
        ExprValidationResult base = validate_expr(*list_access.list, expected_type, false);
        if (!base.type) {
            return { std::nullopt };
        }

        ExprValidationResult index = validate_expr(
            *list_access.index,
            create_or_get_info_ref(
                TypeInfo::create_primitive(arena.string_pool, TypeInfo::BuiltinPrimitive::Int)
            ),
            false
        );

        if (!index.type) {
            return { std::nullopt };
        }

        if (!(*index.type)->is<TypeInfo::Primitive>() ||
            (*index.type)->get_underlying_primitive().primitive !=
                TypeInfo::BuiltinPrimitive::Int) {
            error_handler.error(
                ErrorType::InvalidArrayIndex,
                list_access.index->get_location()
            );
            return { std::nullopt };
        }

        const TypeInfo& list_type = **base.type;
        if (list_type.is<TypeInfo::Array>()) {
            const TypeInfo::Array& array_type = list_type.get<TypeInfo::Array>();
            return { array_type.element };
        } else if (list_type.is<TypeInfo::Vector>()) {
            const TypeInfo::Vector& vector_type = list_type.get<TypeInfo::Vector>();
            return { vector_type.element };
        } else if (list_type.is<TypeInfo::Matrix>()) {
            const TypeInfo::Matrix& matrix_type = list_type.get<TypeInfo::Matrix>();
            return { matrix_type.vector_element };
        } else {
            error_handler.error(
                ErrorType::InvalidArrayAccess,
                list_access.list->get_location()
            );
            return { std::nullopt };
        }
    }

    Ref<TypeInfo> create_or_get_info_ref(TypeInfo&& info) {
        for (const Ref<TypeInfo>& t : arena.types) {
            if (*t == info) {
                return t;
            }
        }
        return arena.alloc(std::move(info));
    }
};
