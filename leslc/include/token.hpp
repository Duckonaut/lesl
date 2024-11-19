#pragma once

#include "stringpool.hpp"

#include <ostream>

enum class TokenType {
    // Single-character tokens
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Dot,
    Plus,
    Semicolon,
    Slash,
    Star,
    Percent,

    // One or two character tokens
    Minus,
    MinusArrow,
    Bang,
    BangEqual,
    Equal,
    EqualEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,

    // Literals
    Identifier,
    Number,

    // Keywords
    Function,
    Attributes,
    Uniform,
    Pipeline,
    Use,

    EndOfFile,
    Error,
};

inline const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::LeftParen:
            return "LeftParen";
        case TokenType::RightParen:
            return "RightParen";
        case TokenType::LeftBrace:
            return "LeftBrace";
        case TokenType::RightBrace:
            return "RightBrace";
        case TokenType::LeftBracket:
            return "LeftBracket";
        case TokenType::RightBracket:
            return "RightBracket";
        case TokenType::Comma:
            return "Comma";
        case TokenType::Dot:
            return "Dot";
        case TokenType::Plus:
            return "Plus";
        case TokenType::Semicolon:
            return "Semicolon";
        case TokenType::Slash:
            return "Slash";
        case TokenType::Star:
            return "Star";
        case TokenType::Percent:
            return "Percent";
        case TokenType::Minus:
            return "Minus";
        case TokenType::MinusArrow:
            return "MinusArrow";
        case TokenType::Bang:
            return "Bang";
        case TokenType::BangEqual:
            return "BangEqual";
        case TokenType::Equal:
            return "Equal";
        case TokenType::EqualEqual:
            return "EqualEqual";
        case TokenType::Greater:
            return "Greater";
        case TokenType::GreaterEqual:
            return "GreaterEqual";
        case TokenType::Less:
            return "Less";
        case TokenType::LessEqual:
            return "LessEqual";
        case TokenType::Identifier:
            return "Identifier";
        case TokenType::Number:
            return "Number";
        case TokenType::Function:
            return "Function";
        case TokenType::Attributes:
            return "Attributes";
        case TokenType::Uniform:
            return "Uniform";
        case TokenType::Pipeline:
            return "Pipeline";
        case TokenType::Use:
            return "Use";
        case TokenType::EndOfFile:
            return "EndOfFile";
        case TokenType::Error:
            return "ERROR";
    }
}

union TokenValue {
    PoolStr str;
    double num;
};

struct Token {
    TokenType type;
    TokenValue value;
};

inline std::ostream& operator<<(std::ostream& out, const Token& token) {
    out << token_type_to_string(token.type);
    if (token.type == TokenType::Identifier) {
        out << ": " << token.value.str.to_string();
    } else if (token.type == TokenType::Number) {
        out << ": " << token.value.num;
    }
    return out;
}
