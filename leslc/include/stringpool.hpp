#pragma once

#include <cstring>
#include <string>

struct PoolStr {
    const char* data;
    size_t size;

    bool operator==(const PoolStr& other) const {
        if (size != other.size) {
            return false;
        }

        return std::memcmp(data, other.data, size) == 0;
    }

    bool operator!=(const PoolStr& other) const {
        return !(*this == other);
    }
};

struct StringPool final {
    char* data;
    size_t size;
    size_t capacity;

    StringPool(size_t capacity) : data(new char[capacity]), size(0), capacity(capacity) {}

    ~StringPool() {
        delete[] data;
    }

    PoolStr add(const char* str, size_t len) {
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

        char* ptr = data + size;
        std::memcpy(ptr, str, len);
        ptr[len] = '\0';
        size += len + 1;

        return {ptr, len};
    }

    PoolStr add(const char* str) {
        return add(str, std::strlen(str));
    }

    PoolStr add(const std::string& str) {
        return add(str.data(), str.size());
    }
};
