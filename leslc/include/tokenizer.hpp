#pragma once

#include "stringpool.hpp"
#include "token.hpp"
#include "unit.hpp"
#include <cctype>

struct Tokenizer final {
    Unit& unit;
    StringPool& pool;

    Tokenizer(StringPool& pool, Unit& unit) : unit(unit), pool(pool) {}

    void skip_whitespace() {
        char c = unit.peek();

        while (std::isspace(c) && !unit.eof()) {
            unit.next();
            c = unit.peek();
        }
    }

    Token next() {
        Token token;

        skip_whitespace();

        if (unit.eof()) {
            token.type = TokenType::EndOfFile;
            return token;
        }

        if (std::isalpha(unit.peek())) {
            token = read_identifier();
        } else if (std::isdigit(unit.peek())) {
            token = read_number();
        } else {
            token.type = TokenType::EndOfFile;
        }

        return token;
    }

    Token read_identifier() {
        Token token;
        token.type = TokenType::Identifier;

        std::string str;

        char c = unit.peek();
        while (std::isalnum(c) && !unit.eof()) {
            str.push_back(unit.next());
            c = unit.peek();
        }

        token.value.str = pool.add(str);

        return token;
    }

    Token read_number() {
        Token token;
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
};
