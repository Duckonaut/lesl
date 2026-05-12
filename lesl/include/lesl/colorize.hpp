#pragma once

#include <ostream>
#include <concepts>

#define COLOR_ENABLE 1

namespace colorize {
enum class Color {
    Reset,
    Bold,
    Dim,
    Italic,
    Underline,
    Blink,
    Invert,
    Hidden,
    Strikethrough,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    BrightBlack,
    BrightRed,
    BrightGreen,
    BrightYellow,
    BrightBlue,
    BrightMagenta,
    BrightCyan,
    BrightWhite,
};

#if COLOR_ENABLE
inline std::ostream& operator<<(std::ostream& out, Color color) {
    switch (color) {
        case Color::Reset: out << "\033[0m"; break;
        case Color::Bold: out << "\033[1m"; break;
        case Color::Dim: out << "\033[2m"; break;
        case Color::Italic: out << "\033[3m"; break;
        case Color::Underline: out << "\033[4m"; break;
        case Color::Blink: out << "\033[5m"; break;
        case Color::Invert: out << "\033[7m"; break;
        case Color::Hidden: out << "\033[8m"; break;
        case Color::Strikethrough: out << "\033[9m"; break;
        case Color::Black: out << "\033[30m"; break;
        case Color::Red: out << "\033[31m"; break;
        case Color::Green: out << "\033[32m"; break;
        case Color::Yellow: out << "\033[33m"; break;
        case Color::Blue: out << "\033[34m"; break;
        case Color::Magenta: out << "\033[35m"; break;
        case Color::Cyan: out << "\033[36m"; break;
        case Color::White: out << "\033[37m"; break;
        case Color::BrightBlack: out << "\033[1;30m"; break;
        case Color::BrightRed: out << "\033[1;31m"; break;
        case Color::BrightGreen: out << "\033[1;32m"; break;
        case Color::BrightYellow: out << "\033[1;33m"; break;
        case Color::BrightBlue: out << "\033[1;34m"; break;
        case Color::BrightMagenta: out << "\033[1;35m"; break;
        case Color::BrightCyan: out << "\033[1;36m"; break;
        case Color::BrightWhite: out << "\033[1;37m"; break;
    }
    return out;
}
#else
inline std::ostream& operator<<(std::ostream& out, Color color) {
    return out;
}
#endif

template <typename T>
concept streamable = requires(std::ostream& os, T t) {
    { os << t };
};

template <streamable T>
struct Colorizer {
    using Self = Colorizer<T>;

    T value;
    Color color;
};

template <streamable T>
std::ostream& operator<<(std::ostream& out, const Colorizer<T>& colorizer) {
    out << colorizer.color << colorizer.value << Color::Reset;
    return out;
}

#define COLORIZE(fn_name, color) \
    inline auto fn_name(auto&& value) { \
        return Colorizer{std::forward<decltype(value)>(value), color}; \
    }

COLORIZE(bold, Color::Bold)
COLORIZE(dim, Color::Dim)
COLORIZE(italic, Color::Italic)
COLORIZE(underline, Color::Underline)
COLORIZE(blink, Color::Blink)
COLORIZE(invert, Color::Invert)
COLORIZE(hidden, Color::Hidden)
COLORIZE(strikethrough, Color::Strikethrough)
COLORIZE(black, Color::Black)
COLORIZE(red, Color::Red)
COLORIZE(green, Color::Green)
COLORIZE(yellow, Color::Yellow)
COLORIZE(blue, Color::Blue)
COLORIZE(magenta, Color::Magenta)
COLORIZE(cyan, Color::Cyan)
COLORIZE(white, Color::White)
COLORIZE(bright_black, Color::BrightBlack)
COLORIZE(bright_red, Color::BrightRed)
COLORIZE(bright_green, Color::BrightGreen)
COLORIZE(bright_yellow, Color::BrightYellow)
COLORIZE(bright_blue, Color::BrightBlue)
COLORIZE(bright_magenta, Color::BrightMagenta)
COLORIZE(bright_cyan, Color::BrightCyan)
COLORIZE(bright_white, Color::BrightWhite)

#undef COLORIZE

}; // namespace colorize
