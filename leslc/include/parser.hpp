#pragma once

#include "error_handler.hpp"
#include "token.hpp"
#include "tokenizer.hpp"
#include "repr.hpp"

struct Parser final {
    Tokenizer& tokenizer;
    ErrorHandler& error_handler;

    Token current;

    Parser(Tokenizer& tokenizer, ErrorHandler& error_handler);
    ~Parser();

    Module parse();

    Decl parse_decl();

    Decl parse_function();
    Decl parse_struct();
    Decl parse_pipeline();

    Stmt parse_stmt();
    Stmt parse_var_decl();
    Stmt parse_return();
    Stmt parse_expr_stmt();

    Expr parse_expr();
    Expr parse_binary();
    Expr parse_unary();
    Expr parse_access_or_call_or_list_access_or_field_access();
    Expr parse_primary();

    void expect(TokenType type);
    void consume(TokenType type);
    void step();
};
