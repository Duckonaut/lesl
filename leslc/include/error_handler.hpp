#pragma once

#include "token.hpp"
#include "unit.hpp"
#include "colorize.hpp"

#include <ostream>
#include <vector>

enum class ErrorType {
    UnexpectedCharacter,
    UnexpectedToken,
};

struct ErrorData {
    bool non_empty;
    union {
        char character;
        TokenType token;
    };

    ErrorData(char character) : non_empty(true), character(character) {}
    ErrorData(TokenType token) : non_empty(true), token(token) {}

    ErrorData() : non_empty(false) {}
};

struct Error {
    ErrorType type;
    ErrorData data;
    SourceLocation location;

    void dump(std::ostream& out) const {
        out << colorize::bold(colorize::bright_red("ERROR")) << ": ";
        switch (type) {
            case ErrorType::UnexpectedCharacter:
                out << "Unexpected character '" << colorize::bold(data.character) << "'";
                break;
            case ErrorType::UnexpectedToken:
                out << "Unexpected token " << colorize::bold(data.token);
                break;
        }

        out << " at " << location.line << ":" << location.column << "\n";
    }
};

struct ErrorHandler {
    inline void error(ErrorType type, char character, SourceLocation location) {
        errors.push_back({ type, { character }, location });
    }

    inline void error(ErrorType type, TokenType token, SourceLocation location) {
        errors.push_back({ type, { token }, location });
    }

    inline void error(ErrorType type, SourceLocation location) {
        errors.push_back({ type, {}, location });
    }

    inline bool has_errors() const {
        return !errors.empty();
    }

    inline void dump(std::ostream& out) const {
        for (const Error& error : errors) {
            error.dump(out);
        }
    }

    std::vector<Error> errors;
};
