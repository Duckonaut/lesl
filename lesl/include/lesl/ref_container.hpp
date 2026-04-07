#pragma once

#include "lesl/tracking_allocator.hpp"

#include <vector>
#include <stdexcept>

template <typename T> struct Ref;

template <typename T>
struct RefContainer {
    std::vector<T, TrackingAllocator<T>> items;
    int32_t generation = 0;

    void clear() {
        items.clear();
        generation++;
    }

    Ref<T> get(size_t index) const {
        return Ref<T>(this, index);
    }

    template <typename... Args>
    Ref<T> emplace(Args&&... args) {
        items.emplace_back(std::forward<Args>(args)...);
        return Ref<T>(this, items.size() - 1);
    }

    size_t size() const {
        return items.size();
    }

    void push_back(T&& t) {
        items.push_back(std::move(t));
    }

    void push_back(const T& t) {
        items.push_back(t);
    }

    struct iterator {
        RefContainer* container;
        size_t index;

        iterator(RefContainer* container, size_t index, int32_t generation)
            : container(container), index(index) {
            if (container->generation != generation) {
                throw std::runtime_error("iterator invalidated");
            }
        }

        Ref<T> operator*() const {
            return Ref<T>(container, index, container->generation);
        }

        iterator& operator++() {
            index++;
            return *this;
        }

        bool operator==(const iterator& other) const {
            return index == other.index && container == other.container && container->generation == other.container->generation;
        }

        bool operator!=(const iterator& other) const {
            return index != other.index || container != other.container || container->generation != other.container->generation;
        }
    };

    iterator begin() {
        return iterator(this, 0, generation);
    }

    iterator end() {
        return iterator(this, items.size(), generation);
    }
};

template <typename T> struct Ref {
    RefContainer<T>* container;
    size_t index;
    int32_t generation;

    Ref(RefContainer<T>* container, size_t index, int32_t generation)
        : container(container), index(index), generation(generation) {}

    Ref(RefContainer<T>* container, size_t index)
        : container(container), index(index), generation(container->generation) {}

    T& operator*() {
        return container->items[index];
    }

    const T& operator*() const {
        return container->items[index];
    }

    T* operator->() {
        return &container->items[index];
    }

    const T* operator->() const {
        return &container->items[index];
    }

    bool operator==(const Ref<T>& other) const {
        return container == other.container && index == other.index && generation == other.generation;
    }

    bool operator!=(const Ref<T>& other) const {
        return container != other.container || index != other.index || generation != other.generation;
    }

    operator bool() const {
        return container != nullptr && index < container->items.size() && generation == container->generation;
    }
};

