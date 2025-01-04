#pragma once

#include "stringpool.hpp"
#include "token.hpp"

#include <cassert>
#include <utility>
#include <vector>

template <typename T>
using Ref = T*;

struct Identifier {
    PoolStr name;
    SourceLocation location;

    Identifier() {}

    Identifier(Token token) {
        assert(token.type == TokenType::Identifier);
        this->name = token.value.str;
        this->location = token.location;
    }
};

struct Module;
struct Decl;
struct Stmt;
struct Expr;

struct TypedIdentifier {
    Identifier name;
    Identifier type;
};

struct Module {
    std::vector<Decl> decls;
};

struct PipelineParameter {
    Identifier name;
    Identifier value;
};

struct Expr {
    enum class Kind {
        Identifier,
        Number,
        Call,
        Unary,
        Binary,
    };

    struct Call {
        Identifier name;
        std::vector<Ref<Expr>> args;
    };

    struct Binary {
        TokenType op;
        Ref<Expr> lhs;
        Ref<Expr> rhs;
    };

    Kind kind;

    union {
        Identifier identifier;
        double number;
        Call call;
        Binary binary;
    };

    Expr(Identifier identifier) : kind(Kind::Identifier), identifier(identifier) {}
    Expr(double number) : kind(Kind::Number), number(number) {}
    Expr(Call call) : kind(Kind::Call), call(call) {}
    Expr(Binary binary) : kind(Kind::Binary), binary(binary) {}
};

struct Stmt {
    enum class Kind {
        Return,
        Var,
        Expr,
    };

    struct Return {
        Ref<Expr> expr;
    };

    struct Var {
        TypedIdentifier typedIdentifier;
        Ref<Expr> expr;
    };

    Kind kind;

    union {
        Return return_;
        Ref<Expr> expr;
        Var var;
    };

    Stmt(Return return_) : kind(Kind::Return), return_(return_) {}
    Stmt(Var var) : kind(Kind::Var), var(var) {}
    Stmt(Ref<Expr> expr) : kind(Kind::Expr), expr(expr) {}
};

struct Decl {
    enum class Kind {
        Import,
        Struct,
        Function,
        Pipeline,
    };

    struct Import {
        PoolStr path;
    };

    struct Struct {
        Identifier name;
        std::vector<TypedIdentifier> members;
    };

    struct Function {
        Identifier name;
        std::vector<TypedIdentifier> params;
        std::vector<TypedIdentifier> rets;
        std::vector<Stmt> stmts;
    };

    struct Pipeline {
        Identifier name;
        std::vector<PipelineParameter> params;
    };

    Kind kind;

    union {
        Import import;
        Struct struct_;
        Function function;
        Pipeline pipeline;
    };

    Decl(Import&& import) : kind(Kind::Import), import(import) {}
    Decl(Struct&& struct_) : kind(Kind::Struct), struct_(struct_) {}
    Decl(Function&& function) : kind(Kind::Function), function(function) {}
    Decl(Pipeline&& pipeline) : kind(Kind::Pipeline), pipeline(pipeline) {}

    inline Decl(Decl& d) {
        this->kind = d.kind;
        switch (d.kind) {
            case Kind::Import:
                this->import = d.import;
                break;
            case Kind::Struct:
                this->struct_ = d.struct_;
                break;
            case Kind::Function:
                this->function = d.function;
                break;
            case Kind::Pipeline:
                this->pipeline = d.pipeline;
                break;
        }
    }
    inline Decl(Decl&& d) {
        this->kind = d.kind;
        switch (d.kind) {
            case Kind::Import:
                this->import = std::move(d.import);
                break;
            case Kind::Struct:
                this->struct_ = std::move(d.struct_);
                break;
            case Kind::Function:
                this->function = std::move(d.function);
                break;
            case Kind::Pipeline:
                this->pipeline = std::move(d.pipeline);
                break;
        }
    }
    inline ~Decl() {
    }
};

