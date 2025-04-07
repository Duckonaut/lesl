#pragma once

#include "arena.hpp"
#include "ref_container.hpp"
#include "repr.hpp"
#include "repr_walker.hpp"
#include "error_handler.hpp"

#include <array>
#include <variant>

struct Validator {
    CompilationArena& arena;
    ErrorHandler& error_handler;
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
                            std::vector<Ref<TypeInfo>> members;
                            for (auto& member : decl->get<Decl::Struct>().members) {
                                members.push_back(create_or_get_info(arena, member.type));
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

                            name = name.substr(0, name.size() - 2);

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

    void validate() {
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

        // fill in type infos
        TypeInfoFiller filler(arena);
        for (Ref<Decl> decl : arena.decls) {
            std::visit(
                [&filler](auto& decl) {
                    filler.visit(decl);
                },
                decl->data
            );
        }
    }

    void validate_function(Decl::Function& f) {
        for (TypedIdentifier& param : f.params) {
            validate_type(param.type);
        }
        for (TypedIdentifier& ret : f.rets) {
            validate_type(ret.type);
        }
        for (Ref<Stmt>& stmt : f.stmts) {
            validate_stmt(*stmt);
        }
    }
    void validate_struct(Decl::Struct& s) {
        for (auto& member : s.members) {
            validate_type(member.type);
        }

        // check for zero-sized arrays that aren't the last member

        for (size_t i = 0; i < s.members.size(); i++) {
            if (s.members[i].type.array_sizes.size() > 0 &&
                s.members[i].type.array_sizes[s.members[i].type.array_sizes.size() - 1] == -1 &&
                i != s.members.size() - 1) {
                error_handler.error(ErrorType::InvalidArraySize, s.members[i].name.location);
            }
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
    void validate_return(Stmt::Return& r) {
        if (r.expr) {
            validate_expr(*r.expr.value());
        }
    }
    void validate_var(Stmt::Var& v) {
        validate_type(v.typedIdentifier.type);
        if (v.expr) {
            validate_expr(*v.expr.value());
        }
    }
    void validate_type(TypeRef& type) {
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
                uint32_t columns = name[name.size() - 1] - '0';
                uint32_t rows = name[name.size() - 3] - '0';

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

    void validate_expr(Expr& expr) {}
};
