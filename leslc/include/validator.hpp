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
    void validate_type(Identifier& type) {
        constexpr std::array<const char*, 14> valid_types = {
            "int",    "uint", "float", "bool", "void",  "float2", "float3",
            "float4", "int2", "int3",  "int4", "uint2", "uint3",  "uint4",
        };
        for (const char* valid_type : valid_types) {
            if (type.name == valid_type) {
                return;
            }
        }

        for (auto decl : arena.decls) {
            if (decl->is<Decl::Struct>()) {
                if (decl->get<Decl::Struct>().name.name == type.name) {
                    return;
                }
            }
        }

        error_handler.error(ErrorType::UnknownType, type.name, type.location);
    }

    void validate_expr(Expr& expr) {

    }
};
