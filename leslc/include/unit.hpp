#pragma once

#include <istream>

struct SourceLocation {
    int line;
    int column;
};

/// @brief A class that represents a single compilation unit.
/// @details Wraps a stream and provides a way to read chars from it while keeping track of the current line and column.
class Unit final {
public:
    std::istream& stream;
    int line = 1;
    int column = 1;

    Unit(std::istream& stream) : stream(stream) {}

    /// @brief Get the next character from the stream.
    /// @return The next character from the stream.
    char next() {
        char c = stream.get();
        if (c == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        return c;
    }

    /// @brief Peek at the next character from the stream.
    /// @return The next character from the stream.
    /// @details This function does not consume the character.
    char peek() {
        return stream.peek();
    }

    /// @brief Check if the stream is at the end.
    /// @return True if the stream is at the end, false otherwise.
    /// @details This function does not consume the character.
    bool eof() {
        return stream.eof();
    }

    /// @brief Get the current source location.
    /// @return The current source location.
    SourceLocation location() {
        return {line, column};
    }
};
