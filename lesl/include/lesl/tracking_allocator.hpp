#pragma once

#include <memory>

template <typename T> struct TrackingAllocator;

struct GlobalTrackingAllocator : std::allocator<void> {
    inline static int32_t current_allocations = 0;
    inline static int32_t total_allocations = 0;
    inline static int32_t max_allocations = 0;

    template <typename T> struct rebind {
        using other = TrackingAllocator<T>;
    };

    template <typename T> T* allocate(size_t n) {
        current_allocations += n;
        total_allocations += n;
        max_allocations = std::max(max_allocations, current_allocations);
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    template <typename T> void deallocate(T* p, size_t n) {
        current_allocations -= n;
        ::operator delete(p);
    }

    static void print_stats() {
        printf("Total allocations: %d\n", total_allocations);
        printf("Max allocations: %d\n", max_allocations);
        printf("Current allocations: %d\n", current_allocations);
    }
};

template <typename T>
struct TrackingAllocator : GlobalTrackingAllocator {
    using value_type = T;

    TrackingAllocator() = default;

    template <typename U>
    TrackingAllocator(const TrackingAllocator<U>&) {}

    T* allocate(size_t n) {
        return GlobalTrackingAllocator::allocate<T>(n);
    }

    void deallocate(T* p, size_t n) {
        GlobalTrackingAllocator::deallocate(p, n);
    }
};
