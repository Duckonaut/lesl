#pragma once

#include "stringpool.hpp"

enum class TokenType {
    Identifier,
    Number,
    EndOfFile,
};

union TokenValue {
    PoolStr str;
    double num;
};

struct Token {
    TokenType type;
    TokenValue value;
};
