#pragma once

#include "ref_container.hpp"
#include "stringpool.hpp"
#include "repr.hpp"

template <typename T> concept ArenaType = std::same_as<T, Decl> || std::same_as<T, Stmt> || std::same_as<T, Expr>;

struct CompilationArena {
    StringPool string_pool;
    RefContainer<Expr> exprs;
    RefContainer<Stmt> stmts;
    RefContainer<Decl> decls;

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

    template <ArenaType T> Ref<T> alloc(T&& t) {
        if constexpr (std::same_as<T, Expr>) {
            exprs.push_back(std::forward<T>(t));
            return Ref<T>(&exprs, exprs.size() - 1, generation);
        } else if constexpr (std::same_as<T, Stmt>) {
            stmts.push_back(std::forward<T>(t));
            return Ref<T>(&stmts, stmts.size() - 1, generation);
        } else if constexpr (std::same_as<T, Decl>) {
            decls.push_back(std::forward<T>(t));
            return Ref<T>(&decls, decls.size() - 1, generation);
        } else {
            static_assert(false, "unsupported type");
        }
    }
};
