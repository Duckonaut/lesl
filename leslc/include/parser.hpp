#pragma once

#include "error_handler.hpp"
#include "token.hpp"
#include "tokenizer.hpp"
#include "repr.hpp"

#include <initializer_list>

struct Parser final {
    Tokenizer& tokenizer;
    ErrorHandler& error_handler;

    Token current;
    Token next;

    Parser(Tokenizer& tokenizer, ErrorHandler& error_handler);
    ~Parser();

    Module parse();

    Decl parse_decl();

    Decl parse_function();
    Decl parse_struct();
    Decl parse_pipeline();

    std::vector<Stmt> parse_stmt_block();
    Stmt parse_return();
    Stmt parse_var();
    Stmt parse_expr_stmt();

    Expr parse_expr();
    Expr parse_assignment_expr();
    Expr parse_logical_or_expr();
    Expr parse_logical_and_expr();
    Expr parse_equality_expr();
    Expr parse_comparison_expr();
    Expr parse_term_expr();
    Expr parse_factor_expr();
    Expr parse_unary();
    Expr parse_access_or_call_or_list_access_or_field_access();
    Expr parse_primary();

    Expr parse_binary_left_assoc_expr(
        std::initializer_list<std::pair<TokenType, Expr::BinaryOp>> ops,
        Expr (Parser::*parse_next)()
    );

    void expect(TokenType type);
    void consume(TokenType type);
    void step();
};
