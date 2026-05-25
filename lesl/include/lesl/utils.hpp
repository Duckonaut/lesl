#pragma once

#include <functional>
#include <utility>

#if __cplusplus >= 202002L
#include <concepts>
#endif

#if __cplusplus >= 202002L
namespace lesl {
template <typename T>
concept Hashable = requires(T a) {
    { std::hash<T>{}(a) } -> std::same_as<size_t>;
};
} // namespace lesl
#endif

/// Implement std::hash for pairs whose elements are hashable

#if __cplusplus >= 202002L
template <lesl::Hashable T1, lesl::Hashable T2> struct std::hash<std::pair<T1, T2>> {
#else
template <typename T1, typename T2> struct std::hash<std::pair<T1, T2>> {
#endif
    size_t operator()(const std::pair<T1, T2>& p) const {
        return std::hash<T1>{}(p.first) ^ std::hash<T2>{}(p.second);
    }
};
