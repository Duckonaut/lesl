#pragma once

#include <vector>

#include "stringpool.hpp"

struct CompilationArena;
template <typename T> struct Ref;

struct Module;
struct Decl;
struct Stmt;
struct Expr;

struct CompilationArena {
    StringPool string_pool;
    std::vector<Expr> exprs;
    std::vector<Stmt> stmts;
    std::vector<Decl> decls;

    int32_t generation = 0;

    CompilationArena() : string_pool(0x1000) {}

    struct Statistics {
        size_t exprs;
        size_t stmts;
        size_t decls;
    };
    
    Statistics statistics() const {
        return { exprs.size(), stmts.size(), decls.size() };
    }

    void clear() {
        exprs.clear();
        stmts.clear();
        decls.clear();

        generation++;
    }

    template <typename T> Ref<T> alloc(T&& t) {
        if constexpr (std::same_as<T, Expr>) {
            exprs.push_back(std::forward<T>(t));
            return Ref<T>(this, exprs.size() - 1, generation);
        } else if constexpr (std::same_as<T, Stmt>) {
            stmts.push_back(std::forward<T>(t));
            return Ref<T>(this, stmts.size() - 1, generation);
        } else if constexpr (std::same_as<T, Decl>) {
            decls.push_back(std::forward<T>(t));
            return Ref<T>(this, decls.size() - 1, generation);
        } else {
            static_assert(false, "unsupported type");
        }
    }
};
