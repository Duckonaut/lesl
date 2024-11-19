#include "stringpool.hpp"

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
