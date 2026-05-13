#pragma once

#include <functional>
#include <utility>

#include <concepts>

namespace lesl {
template <typename T>
concept Hashable = requires(T a) {
    { std::hash<T>{}(a) } -> std::same_as<size_t>;
};
} // namespace lesl

/// Implement std::hash for pairs whose elements are hashable

template <lesl::Hashable T1, lesl::Hashable T2> struct std::hash<std::pair<T1, T2>> {
    size_t operator()(const std::pair<T1, T2>& p) const {
        return std::hash<T1>{}(p.first) ^ std::hash<T2>{}(p.second);
    }
};
