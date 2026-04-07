#pragma once

#include <string>
#include <vector>

struct StringPool;

/// <summary>
/// A string belonging to a string pool.
/// Smaller and way more limited than std::string, but it's enough for our purposes of
/// comparing short identifiers.
/// </summary>
struct PoolStr final {
    const StringPool* pool;
    int poolIndex;

    size_t size() const;

    const char* c_str() const;
    std::string to_string() const;

    bool operator==(const PoolStr& other) const;
    bool operator!=(const PoolStr& other) const;
    bool operator==(const char* other) const;
    bool operator!=(const char* other) const;
    bool operator==(const std::string& other) const;
    bool operator!=(const std::string& other) const;
};

namespace std {
template <> struct hash<PoolStr> {
    size_t operator()(const PoolStr& str) const {
        size_t hash = 0;
        const char* data = str.c_str();
        size_t len = str.size();
        for (size_t i = 0; i < len; i++) {
            hash = data[i] + (hash << 6) + (hash << 16) - hash;
        }
        return hash;
    }
};
} // namespace std

/// <summary>
/// This is a simple string pool implementation.
///
/// Strings are stored in a contiguous memory block, and are immutable.
/// Since this is a compiler, we do very little string manipulation, and mostly will be
/// comparing strings.
/// They also live roughly for the lifetime of a single compilation process, since they will
/// usually represent identifiers in the source code, so we don't need fine-grained control
/// over their lifetime.
/// </summary>
struct StringPool final {
    char* data;
    size_t size;
    size_t capacity;

    struct StringPoolFragment {
        size_t offset;
        size_t length;
    };

    std::vector<StringPoolFragment> fragments;

    StringPool(size_t capacity);

    ~StringPool();

    int find(const char* str, size_t len);
    PoolStr add(const char* str, size_t len);
    PoolStr add(const char* str);
    PoolStr add(const std::string& str);
};
