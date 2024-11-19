#pragma once

#include <cstring>
#include <string>
#include <vector>
#include <cassert>

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
};

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

    inline StringPool(size_t capacity) : data(new char[capacity]), size(0), capacity(capacity) {
        fragments.reserve(0x1000);
    }

    inline ~StringPool() {
        delete[] data;
    }

    inline int find(const char* str, size_t len) {
        for (int i = 0; i < (int)fragments.size(); i++) {
            const StringPoolFragment& fragment = fragments[i];
            if (fragment.length == len && std::memcmp(data + fragment.offset, str, len) == 0) {
                return i;
            }
        }
        return -1;
    }

    inline PoolStr add(const char* str, size_t len) {
        assert(len > 0, "string length must be greater than zero");

        int index = find(str, len);
        if (index != -1) {
            return { this, index };
        }

        if (size + len + 1 > capacity) {
            if (capacity == 0) {
                capacity = 0x1000;
            }
            else {
                capacity *= 2;
            }

            char* new_data = new char[capacity];
            std::memcpy(new_data, data, size);
            delete[] data;
            data = new_data;
        }

        this->fragments.push_back({ size, len });

        char* ptr = data + size;
        std::memcpy(ptr, str, len);
        ptr[len] = '\0';
        size += len + 1;

        PoolStr poolStr = { this, (int)fragments.size() - 1 };

        return poolStr;
    }

    inline PoolStr add(const char* str) {
        return add(str, std::strlen(str));
    }

    inline PoolStr add(const std::string& str) {
        return add(str.data(), str.size());
    }
};
