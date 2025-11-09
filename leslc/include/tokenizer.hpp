#pragma once

#include "error_handler.hpp"
#include "stringpool.hpp"
#include "token.hpp"
#include "unit.hpp"
#include "arena.hpp"
#include <cctype>

struct Tokenizer final {
    Unit& unit;
    CompilationArena& arena;
    ErrorHandler& error_handler;

    Tokenizer(CompilationArena& arena, Unit& unit, ErrorHandler& error_handler)
        : unit(unit), arena(arena), error_handler(error_handler) {}

    void skip_whitespace() {
        char c = unit.peek();

        while (std::isspace(c) && !unit.eof()) {
            unit.next();
            c = unit.peek();
        }
    }

    Token next() {
        Token token{};

        skip_whitespace();

        if (unit.eof()) {
            token.type = TokenType::EndOfFile;
            token.location = unit.location();
            return token;
        }

        char c = unit.peek();
        SourceLocation location = unit.location();
        if (std::isalpha(unit.peek())) {
            token = read_keyword_or_identifier();
        } else if (std::isdigit(unit.peek())) {
            token = read_number();
        } else {
            token = read_symbol();
        }

        token.location = location;

        if (token.type == TokenType::Error) {
            error_handler.error(ErrorType::UnexpectedCharacter, c, location);
        }

        return token;
    }

    Token read_keyword_or_identifier() {
        Token token{};
        token.type = TokenType::Identifier;

        std::string str;

        char c = unit.peek();
        while ((std::isalnum(c) || c == '_') && !unit.eof()) {
            str.push_back(unit.next());
            c = unit.peek();
        }

        if (str == "function") {
            token.type = TokenType::Function;
        } else if (str == "struct") {
            token.type = TokenType::Struct;
        } else if (str == "pipeline") {
            token.type = TokenType::Pipeline;
        } else if (str == "import") {
            token.type = TokenType::Import;
        } else if (str == "return") {
            token.type = TokenType::Return; 
        } else if (str == "if") {
            token.type = TokenType::If;
        } else if (str == "else") {
            token.type = TokenType::Else;
        }

        if (token.type == TokenType::Identifier) {
            token.value.str = arena.string_pool.add(str);
        }

        return token;
    }

    Token read_number() {
        Token token{};
        token.type = TokenType::Number;

        std::string str;

        char c = unit.peek();
        while (std::isdigit(c) && !unit.eof()) {
            str.push_back(unit.next());
            c = unit.peek();
        }

        if (c == '.') {
            str.push_back(unit.next());
            c = unit.peek();
            while (std::isdigit(c) && !unit.eof()) {
                str.push_back(unit.next());
                c = unit.peek();
            }
        }

        token.value.num = std::stod(str);

        return token;
    }

#define SINGLE_CHAR_TOKEN(c, t)                                                                \
    case c:                                                                                    \
        token.type = t;                                                                        \
        break;

#define SINGLE_OR_DOUBLE_CHAR_TOKEN(c1, c2, t1, t2)                                            \
    case c1:                                                                                   \
        token.type = t1;                                                                       \
        if (unit.peek() == c2) {                                                               \
            unit.next();                                                                       \
            token.type = t2;                                                                   \
        }                                                                                      \
        break;

    Token read_symbol() {
        Token token{};

        char c = unit.next();

        switch (c) {
            SINGLE_CHAR_TOKEN('(', TokenType::LeftParen)
            SINGLE_CHAR_TOKEN(')', TokenType::RightParen)
            SINGLE_CHAR_TOKEN('{', TokenType::LeftBrace)
            SINGLE_CHAR_TOKEN('}', TokenType::RightBrace)
            SINGLE_CHAR_TOKEN('[', TokenType::LeftBracket)
            SINGLE_CHAR_TOKEN(']', TokenType::RightBracket)
            SINGLE_CHAR_TOKEN(',', TokenType::Comma)
            SINGLE_CHAR_TOKEN('.', TokenType::Dot)
            SINGLE_CHAR_TOKEN(';', TokenType::Semicolon)
            case '-':
                token.type = TokenType::Minus;
                if (unit.peek() == '>') {
                    unit.next();
                    token.type = TokenType::MinusArrow;
                } else if (unit.peek() == '=') {
                    unit.next();
                    token.type = TokenType::MinusEqual;
                }
                break;
                SINGLE_OR_DOUBLE_CHAR_TOKEN('+', '=', TokenType::Plus, TokenType::PlusEqual)
                SINGLE_OR_DOUBLE_CHAR_TOKEN('/', '=', TokenType::Slash, TokenType::SlashEqual)
                SINGLE_OR_DOUBLE_CHAR_TOKEN('*', '=', TokenType::Star, TokenType::StarEqual)
                SINGLE_OR_DOUBLE_CHAR_TOKEN(
                    '%',
                    '=',
                    TokenType::Percent,
                    TokenType::PercentEqual
                )
                SINGLE_OR_DOUBLE_CHAR_TOKEN('!', '=', TokenType::Bang, TokenType::BangEqual)
                SINGLE_OR_DOUBLE_CHAR_TOKEN('=', '=', TokenType::Equal, TokenType::EqualEqual)
                SINGLE_OR_DOUBLE_CHAR_TOKEN(
                    '>',
                    '=',
                    TokenType::Greater,
                    TokenType::GreaterEqual
                )
                SINGLE_OR_DOUBLE_CHAR_TOKEN('<', '=', TokenType::Less, TokenType::LessEqual)
            default:
                token.type = TokenType::Error;
        }

        return token;
    }
};
