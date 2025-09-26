/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019-2023, Raspberry Pi Ltd
 *
 * NothrowAny - std::any replacement
 *
 * This custom class is a replacement for std::any which uses
 * dynamic allocation
 */

#ifndef NO_THROW_ANY_H
#define NO_THROW_ANY_H

#include <type_traits>
#include <typeinfo>
#include <utility>
#include <new>
#include <cstring>
#include <cstddef>
#include <cassert>

class NothrowAny {
public:
    NothrowAny() noexcept
        : mType(&typeid(void)),
          mDestroyFn(nullptr),
          mCopyFn(nullptr),
          mMoveFn(nullptr)
    {}

    ~NothrowAny() noexcept
    {
        destroy();
    }

    // Copy constructor
    NothrowAny(const NothrowAny& other)
    {
        mType = &typeid(void);
        mDestroyFn = nullptr;
        mCopyFn = nullptr;
        mMoveFn = nullptr;
        if (other.has_value()) {
            other.mCopyFn(&other.mStorage, &mStorage);
            mType = other.mType;
            mDestroyFn = other.mDestroyFn;
            mCopyFn = other.mCopyFn;
            mMoveFn = other.mMoveFn;
        }
    }

    // Move constructor
    NothrowAny(NothrowAny&& other) noexcept
    {
        mType = &typeid(void);
        mDestroyFn = nullptr;
        mCopyFn = nullptr;
        mMoveFn = nullptr;
        if (other.has_value()) {
            other.mMoveFn(&other.mStorage, &mStorage);
            mType = other.mType;
            mDestroyFn = other.mDestroyFn;
            mCopyFn = other.mCopyFn;
            mMoveFn = other.mMoveFn;

            // leave other empty
            other.mDestroyFn = nullptr;
            other.mCopyFn = nullptr;
            other.mMoveFn = nullptr;
            other.mType = &typeid(void);
        }
    }

    // Copy assignment
    NothrowAny& operator=(const NothrowAny& other)
    {
        if (this == &other) {
            return *this;
        }
        NothrowAny tmp(other);
        swap(tmp);
        return *this;
    }

    // Move assignment
    NothrowAny& operator=(NothrowAny&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        destroy();
        if (other.has_value()) {
            other.mMoveFn(&other.mStorage, &mStorage);
            mType = other.mType;
            mDestroyFn = other.mDestroyFn;
            mCopyFn = other.mCopyFn;
            mMoveFn = other.mMoveFn;

            other.mDestroyFn = nullptr;
            other.mCopyFn = nullptr;
            other.mMoveFn = nullptr;
            other.mType = &typeid(void);
        } else {
            mType = &typeid(void);
            mDestroyFn = nullptr;
            mCopyFn = nullptr;
            mMoveFn = nullptr;
        }
        return *this;
    }

    // Construct from value
    template<typename T>
    NothrowAny(T&& value)
    {
        static_assert(sizeof(std::remove_cv_t<std::remove_reference_t<T>>) <= SBO_SIZE,
                      "Type too large for NothrowAny");
        mType = &typeid(void);
        mDestroyFn = nullptr;
        mCopyFn = nullptr;
        mMoveFn = nullptr;
        store(std::forward<T>(value));
    }

    // Store a new value and replace the existing value
    template<typename T>
    void set(T&& value)
    {
        static_assert(sizeof(std::remove_cv_t<std::remove_reference_t<T>>) <= SBO_SIZE,
                      "Type too large for NothrowAny");
        store(std::forward<T>(value));
    }

    // Non-throwing pointer-style cast
    template<typename T>
    T* any_cast() noexcept
    {
        if (*mType != typeid(T)) {
            return nullptr;
        }
        return reinterpret_cast<T*>(&mStorage);
    }

    template<typename T>
    const T* any_cast() const noexcept
    {
        if (*mType != typeid(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(&mStorage);
    }

    // check
    bool has_value() const noexcept
    {
        return *mType != typeid(void);
    }

    // swap
    void swap(NothrowAny& other) noexcept
    {
        // Swap buffers by using std::move
        if (this == &other) return;

        NothrowAny tmp(std::move(*this));
        *this = std::move(other);
        other = std::move(tmp);
    }

private:
    static constexpr size_t SBO_SIZE = 256;
    using Storage = typename std::aligned_storage<SBO_SIZE>::type;
    Storage mStorage;
    const std::type_info* mType;

    void (*mDestroyFn)(void*) noexcept;
    void (*mCopyFn)(const void* src, void* dest);
    void (*mMoveFn)(void* src, void* dest) noexcept;

    // Destroy stored object if any
    void destroy() noexcept
    {
        if (mDestroyFn) {
            mDestroyFn(&mStorage);
            mDestroyFn = nullptr;
        }
        mType = &typeid(void);
        mCopyFn = nullptr;
        mMoveFn = nullptr;
    }

    template<typename T>
    static void destroyImpl(void* p) noexcept
    {
        reinterpret_cast<T*>(p)->~T();
    }

    template<typename T>
    static void copyImpl(const void* src, void* dest)
    {
        new (dest) T(*reinterpret_cast<const T*>(src));
    }

    template<typename T>
    static void moveImpl(void* src, void* dest) noexcept
    {
        new (dest) T(std::move(*reinterpret_cast<T*>(src)));
        // destroy source
        reinterpret_cast<T*>(src)->~T();
    }

    template<typename T>
    void setOpsAndType() noexcept
    {
        mDestroyFn = &destroyImpl<T>;
        mCopyFn = &copyImpl<T>;
        mMoveFn = &moveImpl<T>;
        mType = &typeid(T);
    }

    template<typename T>
    void store(T&& value)
    {
        // Destroy previous
        destroy();

        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        new (&mStorage) U(std::forward<T>(value));
        setOpsAndType<U>();
    }
};

#endif
