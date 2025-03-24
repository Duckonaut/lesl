#pragma once

#include "ref_container.hpp"
#include "stringpool.hpp"
#include "repr.hpp"

#include <concepts>

template <typename T>
concept ArenaType = std::same_as<T, Decl> || std::same_as<T, Stmt> || std::same_as<T, Expr> ||
                    std::same_as<T, TypeInfo>;

/// The CompilationArena is a container for all the objects that are created during the compilation process.
/// As of right now, it contains the following objects:
/// - StringPool: a pool of strings that are used to store identifiers and literals.
/// - RefContainers for Expr, Stmt, and Decl: these are used to store the object tree nodes, and are used to manage the memory of these objects.
/// - Generation: a counter that is incremented every time the arena is cleared. This is used to invalidate all the Refs that are created from the arena,
/// but keep the memory allocated for subsequent passes, hopefully speeding them up.

struct CompilationArena {
    StringPool string_pool;
    RefContainer<Expr> exprs;
    RefContainer<Stmt> stmts;
    RefContainer<Decl> decls;
    RefContainer<TypeInfo> types;

    int32_t generation = 0;

    CompilationArena() : string_pool(0x1000) {}

    struct Statistics {
        size_t exprs;
        size_t stmts;
        size_t decls;
        size_t types;
    };

    Statistics statistics() const {
        return { exprs.size(), stmts.size(), decls.size(), types.size() };
    }

    void clear() {
        exprs.clear();
        stmts.clear();
        decls.clear();
        types.clear();

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
        } else if constexpr (std::same_as<T, TypeInfo>) {
            types.push_back(std::forward<T>(t));
            return Ref<T>(&types, types.size() - 1, generation);
        } else {
            static_assert(false, "unsupported type");
        }
    }
};
