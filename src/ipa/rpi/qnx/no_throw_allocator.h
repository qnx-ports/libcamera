/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019-2023, Raspberry Pi Ltd
 *
 * Raspberry Pi IPA
 *
 * This custom allocator is for supplying std::nothrow
 * to std::vector to avoid QNX slog2 being flooded with
 * warning messages
 */

#ifndef NO_THROW_ALLOCATOR_H
#define NO_THROW_ALLOCATOR_H

#include <new>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <memory>

template <typename T>
struct NothrowAllocator {
    using value_type = T;

    // Default constructor
    NothrowAllocator() noexcept = default;

    // Copy from other type
    template <class U>
    NothrowAllocator(const NothrowAllocator<U>&) noexcept {}

    // Allocate memory using ::operator new (std::nothrow)
    T* allocate(std::size_t size)
    {
        if (size == 0) {
            return nullptr;
        }
        // Prevent overflow
        if (size > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        void* ptr = ::operator new(size * sizeof(T), std::nothrow);
        if (!ptr) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(ptr);
    }

    // Deallocate memory
    void deallocate(T* ptr, std::size_t) noexcept
    {
        ::operator delete(ptr);
    }

    bool operator==(const NothrowAllocator&) const noexcept
    {
        return true;
    }
    bool operator!=(const NothrowAllocator&) const noexcept
    {
        return false;
    }
};


#endif
