#include "lesl/stringpool.hpp"

#include <cstring>
#include <cassert>

namespace lesl {
const char* PoolStr::c_str() const {
    return pool->data + this->pool->fragments[this->poolIndex].offset;
}

std::string PoolStr::to_string() const {
    return std::string(c_str(), size());
}

size_t PoolStr::size() const {
    return this->pool->fragments[this->poolIndex].length;
}

bool PoolStr::operator==(const PoolStr& other) const {
    return this->pool == other.pool && this->poolIndex == other.poolIndex;
}

bool PoolStr::operator!=(const PoolStr& other) const {
    return !(*this == other);
}

bool PoolStr::operator==(const char* other) const {
    const char* str = c_str();
    size_t len = size();
    for (size_t i = 0; i < len; i++) {
        if (str[i] != other[i]) {
            return false;
        }
    }
    return other[len] == '\0';
}

bool PoolStr::operator!=(const char* other) const {
    return !(*this == other);
}

bool PoolStr::operator==(const std::string& other) const {
    if (other.size() != size()) {
        return false;
    }
    return std::memcmp(c_str(), other.data(), size()) == 0;
}

bool PoolStr::operator!=(const std::string& other) const {
    return !(*this == other);
}

bool PoolStr::operator<(const PoolStr& other) const {
    return poolIndex < other.poolIndex;
}

StringPool::StringPool(size_t capacity)
    : data(new char[capacity]), size(0), capacity(capacity) {
    fragments.reserve(0x1000);
}

StringPool::~StringPool() {
    delete[] data;
}

int StringPool::find(const char* str, size_t len) {
    for (int i = 0; i < (int)fragments.size(); i++) {
        const StringPoolFragment& fragment = fragments[i];
        if (fragment.length == len && std::memcmp(data + fragment.offset, str, len) == 0) {
            return i;
        }
    }
    return -1;
}

PoolStr StringPool::add(const char* str, size_t len) {
    assert(len > 0);
    int index = find(str, len);
    if (index != -1) {
        return { this, index };
    }
    if (size + len + 1 > capacity) {
        if (capacity == 0) {
            capacity = 0x1000;
        } else {
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

PoolStr StringPool::add(const char* str) {
    return add(str, std::strlen(str));
}

PoolStr StringPool::add(const std::string& str) {
    return add(str.data(), str.size());
}
}; // namespace lesl
