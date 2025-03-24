#pragma once

#include "arena.hpp"
#include "repr.hpp"
#include "error_handler.hpp"

#include <array>

struct Validator {
    CompilationArena& arena;
    ErrorHandler& error_handler;
    Validator(CompilationArena& arena, ErrorHandler& error_handler)
        : arena(arena), error_handler(error_handler) {}
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
                s.members[i].type.array_sizes[s.members[i].type.array_sizes.size() - 1] == 0 &&
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
            "Vertex", "Fragment",
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
                error_handler.error(ErrorType::MissingPipelineParameter, arena.string_pool.add(necessary_param), p.name.location);
            }
        }
    }
    void validate_stmt(Stmt& stmt) {
        if (stmt.is<Stmt::Return>()) {
            validate_return(stmt.get<Stmt::Return>());
        } else if (stmt.is<Stmt::Var>()) {
            validate_var(stmt.get<Stmt::Var>());
        } else if (stmt.is<Ref<Expr>>()) {
            validate_expr(*stmt.get<Ref<Expr>>());
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
        for (uint32_t i = 0; i < type.array_sizes.size(); i++) {
            if (type.array_sizes[i] == 0 && i != type.array_sizes.size() - 1) {
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

        if (is_vector || is_matrix) {
            if (name == "float" || name == "int" || name == "uint") {
                return;
            } else {
                error_handler.error(ErrorType::InvalidCompoundBaseType, type.name.name, type.name.location);

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

    void validate_expr(Expr& expr) {

    }
};
