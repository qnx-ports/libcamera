/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019-2021, Raspberry Pi Ltd
 *
 * general metadata class
 */
#pragma once

/* A simple class for carrying arbitrary metadata, for example about an image. */

#include <any>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include <libcamera/base/thread_annotations.h>

#ifdef __QNX__
#include <no_throw_any.h>
#endif

namespace RPiController {

class LIBCAMERA_TSA_CAPABILITY("mutex") Metadata
{
public:
	Metadata() = default;

	Metadata(Metadata const &other)
	{
		std::scoped_lock otherLock(other.mutex_);
		data_ = other.data_;
	}

	Metadata(Metadata &&other)
	{
		std::scoped_lock otherLock(other.mutex_);
		data_ = std::move(other.data_);
		other.data_.clear();
	}

    template<typename T>
    void set(const std::string &tag, T&& value) noexcept {
        std::scoped_lock lock(mutex_);
        data_[tag].set(std::forward<T>(value));
    }

#ifdef __QNX__
	template<typename T>
    int get(const std::string &tag, T &value) const noexcept
    {
        auto it = data_.find(tag);
        if (it == data_.end()) {
            return -1;
        }

        if (auto ptr = it->second.any_cast<T>())
        {   // Safe, returns nullptr if mismatch
            value = *ptr;
            return 0;
        }

        // Type mismatch
        return -2;
    }
#else
	template<typename T>
	int get(std::string const &tag, T &value) const
	{
		std::scoped_lock lock(mutex_);
		auto it = data_.find(tag);
		if (it == data_.end())
			return -1;
		value = std::any_cast<T>(it->second);
		return 0;
	}
#endif

	void clear()
	{
		std::scoped_lock lock(mutex_);
		data_.clear();
	}

	Metadata &operator=(Metadata const &other)
	{
		std::scoped_lock lock(mutex_, other.mutex_);
		data_ = other.data_;
		return *this;
	}

	Metadata &operator=(Metadata &&other)
	{
		std::scoped_lock lock(mutex_, other.mutex_);
		data_ = std::move(other.data_);
		other.data_.clear();
		return *this;
	}

	void merge(Metadata &other)
	{
		std::scoped_lock lock(mutex_, other.mutex_);
		data_.merge(other.data_);
	}

	void mergeCopy(const Metadata &other)
	{
		std::scoped_lock lock(mutex_, other.mutex_);
		/*
		 * If the metadata key exists, ignore this item and copy only
		 * unique key/value pairs.
		 */
		data_.insert(other.data_.begin(), other.data_.end());
	}

	void erase(std::string const &tag)
	{
		std::scoped_lock lock(mutex_);
		eraseLocked(tag);
	}

	template<typename T>
	T *getLocked(std::string const &tag)
	{
		/*
		 * This allows in-place access to the Metadata contents,
		 * for which you should be holding the lock.
		 */
		auto it = data_.find(tag);
		if (it == data_.end())
			return nullptr;
#ifdef __QNX__
		// Use NowthrowAny's custom any_cast function
		return it->second.any_cast<T>();
#else
		return std::any_cast<T>(&it->second);
#endif
	}

	template<typename T>
	void setLocked(std::string const &tag, T &&value)
	{
		/* Use this only if you're holding the lock yourself. */
		data_[tag] = std::forward<T>(value);
	}

	void eraseLocked(std::string const &tag)
	{
		auto it = data_.find(tag);
		if (it == data_.end())
			return;
		data_.erase(it);
	}

	/*
	 * Note: use of (lowercase) lock and unlock means you can create scoped
	 * locks with the standard lock classes.
	 * e.g. std::lock_guard<RPiController::Metadata> lock(metadata)
	 */
	void lock() LIBCAMERA_TSA_ACQUIRE() { mutex_.lock(); }
	auto try_lock() LIBCAMERA_TSA_ACQUIRE() { return mutex_.try_lock(); }
	void unlock() LIBCAMERA_TSA_RELEASE() { mutex_.unlock(); }

private:
	mutable std::mutex mutex_;
#ifdef __QNX__
	using MapType = std::map<
        std::string,
        NothrowAny,
        std::less<std::string>,
        NothrowAllocator<std::pair<const std::string, NothrowAny>>>;
    MapType data_;
#else
	std::map<std::string, std::any> data_;
#endif
};

} /* namespace RPiController */
