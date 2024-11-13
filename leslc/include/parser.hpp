#pragma once

#include "token.hpp"
#include "tokenizer.hpp"

struct Parser final {
    Tokenizer& tokenizer;

    Parser(Tokenizer& tokenizer) : tokenizer(tokenizer) {}

    void parse() {
        Token token = tokenizer.next();

        while (token.type != TokenType::EndOfFile) {
            if (token.type == TokenType::Identifier) {
                // Do something with the identifier
            } else if (token.type == TokenType::Number) {
                // Do something with the number
            }

            token = tokenizer.next();
        }
    }
};
