/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * Logging infrastructure
 */

#pragma once

#include <atomic>
#include <sstream>
#include <string_view>

#include <libcamera/base/private.h>

#include <libcamera/base/class.h>
#include <libcamera/base/utils.h>

namespace libcamera {

enum LogSeverity {
	LogInvalid = -1,
	LogDebug = 0,
	LogInfo,
	LogWarning,
	LogError,
	LogFatal,
};

class LogCategory
{
public:
	static LogCategory *create(std::string_view name);

	const std::string &name() const { return name_; }
	LogSeverity severity() const { return severity_.load(std::memory_order_relaxed); }
	void setSeverity(LogSeverity severity) { severity_.store(severity, std::memory_order_relaxed); }

	static const LogCategory &defaultCategory();

private:
	friend class Logger;
	explicit LogCategory(std::string_view name);

	const std::string name_;

	std::atomic<LogSeverity> severity_;
	static_assert(decltype(severity_)::is_always_lock_free);
};

#ifdef __QNX__
// Define these macros to be nothing
#define LOG_DECLARE_CATEGORY(name)
#define LOG_DEFINE_CATEGORY(name)
#else
#define LOG_DECLARE_CATEGORY(name)					\
extern const LogCategory &_LOG_CATEGORY(name)();

#define LOG_DEFINE_CATEGORY(name)					\
LOG_DECLARE_CATEGORY(name)						\
const LogCategory &_LOG_CATEGORY(name)()				\
{									\
	/* The instance will be deleted by the Logger destructor. */	\
	static LogCategory *category = LogCategory::create(#name);	\
	return *category;						\
}
#endif

class LogMessage
{
public:
	LogMessage(const char *fileName, unsigned int line,
		   const LogCategory &category, LogSeverity severity,
		   std::string prefix = {});
	~LogMessage();

	std::ostream &stream() { return msgStream_; }

	const utils::time_point &timestamp() const { return timestamp_; }
	LogSeverity severity() const { return severity_; }
	const LogCategory &category() const { return category_; }
	const std::string &fileInfo() const { return fileInfo_; }
	const std::string &prefix() const { return prefix_; }
	const std::string msg() const { return msgStream_.str(); }

private:
	LIBCAMERA_DISABLE_COPY_AND_MOVE(LogMessage)

	std::ostringstream msgStream_;
	const LogCategory &category_;
	LogSeverity severity_;
	utils::time_point timestamp_;
	std::string fileInfo_;
	std::string prefix_;
};

class Loggable
{
public:
	virtual ~Loggable();

protected:
	virtual std::string logPrefix() const = 0;

	LogMessage _log(const LogCategory *category, LogSeverity severity,
			const char *fileName = __builtin_FILE(),
			unsigned int line = __builtin_LINE()) const;
};

LogMessage _log(const LogCategory *category, LogSeverity severity,
		const char *fileName = __builtin_FILE(),
		unsigned int line = __builtin_LINE());

#ifdef __QNX__
#include <log_handler.h>
#include <ostream>
#include <cstdio>
#include <cstring>

// Use FixedBuffer instead of ostringstream to avoid
// string dynamic allocation
struct FixedBuffer : std::streambuf {
    FixedBuffer(char* buf, size_t len)
    {
        setp(buf, buf + len - 1);
    }

    // Handle overflow
    int_type overflow(int_type ch) override
    {
        if (ch != traits_type::eof() && pptr() < epptr())
        {
            *pptr() = ch;
            pbump(1);
            return ch;
        }
        return traits_type::eof();
    }

    const char* c_str()
    {
        *pptr() = '\0';
        return pbase();
    }
};

// Wrapper for QNX slog2 LOG_* macros
struct LogStreamAdapter {
    using LogFunc = void (*)(const char*);
    LogFunc log_func;

    static constexpr size_t BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE];
    FixedBuffer fb;
    std::ostream stream;

    LogStreamAdapter(LogFunc f) noexcept
        : log_func(f), fb(buffer, BUFFER_SIZE), stream(&fb) {}

    template<typename T>
    LogStreamAdapter& operator<<(const T& value) noexcept
    {
        stream << value;
        return *this;
    }

    ~LogStreamAdapter() noexcept
    {
        log_func(fb.c_str());
    }
};

#define LOG_HELPER_Warning(...) LOG_WARNING(__VA_ARGS__)
#define LOG_HELPER_Debug(...)   LOG_DEBUG1(__VA_ARGS__)
#define LOG_HELPER_Fatal(...)   LOG_ERROR(__VA_ARGS__)
#define LOG_HELPER_Error(...)   LOG_ERROR(__VA_ARGS__)
#define LOG_HELPER_Info(...)    LOG_INFO(__VA_ARGS__)

#define LOG_DISPATCH(level, ...) LOG_HELPER_##level(__VA_ARGS__)
// Define LOG macro to use the LogStreamAdapter class
// LogStreamAdpater then uses LOG_DISPATCH which translates to slog2 macros
#define LOG(category, level) \
    LogStreamAdapter(+[](const char* s){ LOG_DISPATCH(level, "%s", s); })
#else
#ifndef __DOXYGEN__
#define _LOG_CATEGORY(name) logCategory##name

#define _LOG1(severity) \
	_log(nullptr, Log##severity).stream()
#define _LOG2(category, severity) \
	_log(&_LOG_CATEGORY(category)(), Log##severity).stream()

/*
 * Expand the LOG() macro to _LOG1() or _LOG2() based on the number of
 * arguments.
 */
#define _LOG_MACRO(_1, _2, NAME, ...) NAME
#define LOG(...) _LOG_MACRO(__VA_ARGS__, _LOG2, _LOG1)(__VA_ARGS__)
#else /* __DOXYGEN___ */
#define LOG(category, severity)
#endif /* __DOXYGEN__ */
#endif

#ifndef NDEBUG
#define ASSERT(condition) static_cast<void>(({                          \
	if (!(condition))                                               \
		LOG(Fatal) << "assertion \"" #condition "\" failed in " \
			   << __func__ << "()";                         \
}))
#else
#define ASSERT(condition) static_cast<void>(false && (condition))
#endif

} /* namespace libcamera */
