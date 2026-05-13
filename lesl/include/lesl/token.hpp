#pragma once

#include "lesl/stringpool.hpp"
#include "lesl/unit.hpp"

#include <ostream>

namespace lesl {
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
    Semicolon,
    Colon,

    // One or two character tokens
    Minus,
    MinusArrow,
    MinusEqual,
    Plus,
    PlusEqual,
    Slash,
    SlashEqual,
    Star,
    StarEqual,
    Percent,
    PercentEqual,
    Bang,
    BangEqual,
    Equal,
    EqualEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Pipe,
    PipePipe,
    Amp,
    AmpAmp,

    // Literals
    Identifier,
    Number,

    // Keywords
    Function,
    Struct,
    Pipeline,
    Import,
    Return,
    If,
    Else,
    For,
    Break,
    Continue,
    Discard,

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
        case TokenType::Semicolon:
            return "Semicolon";
        case TokenType::Colon:
            return "Colon";
        case TokenType::Minus:
            return "Minus";
        case TokenType::MinusArrow:
            return "MinusArrow";
        case TokenType::MinusEqual:
            return "MinusEqual";
        case TokenType::Plus:
            return "Plus";
        case TokenType::PlusEqual:
            return "PlusEqual";
        case TokenType::Slash:
            return "Slash";
        case TokenType::SlashEqual:
            return "SlashEqual";
        case TokenType::Star:
            return "Star";
        case TokenType::StarEqual:
            return "StarEqual";
        case TokenType::Percent:
            return "Percent";
        case TokenType::PercentEqual:
            return "PercentEqual";
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
        case TokenType::Pipe:
            return "Pipe";
        case TokenType::PipePipe:
            return "PipePipe";
        case TokenType::Amp:
            return "Amp";
        case TokenType::AmpAmp:
            return "AmpAmp";
        case TokenType::Identifier:
            return "Identifier";
        case TokenType::Number:
            return "Number";
        case TokenType::Function:
            return "Function";
        case TokenType::Struct:
            return "Struct";
        case TokenType::Pipeline:
            return "Pipeline";
        case TokenType::Import:
            return "Import";
        case TokenType::Return:
            return "Return";
        case TokenType::If:
            return "If";
        case TokenType::Else:
            return "Else";
        case TokenType::For:
            return "For";
        case TokenType::Break:
            return "Break";
        case TokenType::Continue:
            return "Continue";
        case TokenType::Discard:
            return "Discard";
        case TokenType::EndOfFile:
            return "EndOfFile";
        case TokenType::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

union TokenValue {
    PoolStr str;
    double num;
};

struct Token {
    TokenType type;
    TokenValue value;
    SourceLocation location;
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

inline std::ostream& operator<<(std::ostream& out, const TokenType& type) {
    out << token_type_to_string(type);
    return out;
}
} // namespace lesl
