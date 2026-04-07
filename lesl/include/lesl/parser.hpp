#pragma once

#include "lesl/error_handler.hpp"
#include "lesl/token.hpp"
#include "lesl/repr.hpp"
#include "lesl/tokenizer.hpp"

#include <initializer_list>

struct Parser final {
    Tokenizer& tokenizer;
    CompilationArena& arena;
    ErrorHandler& error_handler;

    Token current;
    Token next;

    Parser(CompilationArena& arena, Tokenizer& tokenizer, ErrorHandler& error_handler);
    ~Parser();

    void parse();

    Ref<Decl> parse_decl();

    Ref<Decl> parse_function();
    Ref<Decl> parse_struct();
    Ref<Decl> parse_pipeline();

    std::vector<Ref<Stmt>> parse_stmt_block();
    Ref<Stmt> parse_return();
    Ref<Stmt> parse_var();
    Ref<Stmt> parse_if_stmt();
    Ref<Stmt> parse_for_stmt();
    Ref<Stmt> parse_break();
    Ref<Stmt> parse_continue();
    Ref<Stmt> parse_expr_stmt();

    Ref<Expr> parse_expr();
    Ref<Expr> parse_assignment_expr();
    Ref<Expr> parse_logical_or_expr();
    Ref<Expr> parse_logical_and_expr();
    Ref<Expr> parse_equality_expr();
    Ref<Expr> parse_comparison_expr();
    Ref<Expr> parse_term_expr();
    Ref<Expr> parse_factor_expr();
    Ref<Expr> parse_unary();
    Ref<Expr> parse_access_or_call_or_list_access_or_field_access();
    Ref<Expr> parse_primary();

    Ref<Expr> parse_binary_left_assoc_expr(
        std::initializer_list<std::pair<TokenType, Expr::BinaryOp>> ops,
        Ref<Expr> (Parser::*parse_next)()
    );

    TypeRef parse_type_ref();

    void expect(TokenType type);
    void consume(TokenType type);
    void step();
};
