/*
 * Copyright (c) 2024 BlackBerry Limited. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file log_handler.h
 *
 * @brief Implements the logging for the application
 *
 */

#ifndef _LOG_HANDLER_H_
#define _LOG_HANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/slog2.h>

/**
 * @brief Macros to add a log at a given severity level
 * @details
 * Call the appropriate macro to add a log entry at a given severity level; the
 * log will only be visible if this severity level is below or equal our
 * verbosity level.  Macros add in function name and line number to each log
 * entry.
 *
 * @param format printf-style format string
 * @param args variable argument list
 */
#define LOG_CRITICAL(format, args...) \
    loghEntry(SLOG2_CRITICAL, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_ERROR(format, args...) \
    loghEntry(SLOG2_ERROR, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_WARNING(format, args...) \
    loghEntry(SLOG2_WARNING, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_NOTICE(format, args...) \
    loghEntry(SLOG2_NOTICE, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_INFO(format, args...) \
    loghEntry(SLOG2_INFO, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_DEBUG1(format, args...) \
    loghEntry(SLOG2_DEBUG1, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

#define LOG_DEBUG2(format, args...) \
    loghEntry(SLOG2_DEBUG2, "%s(%d): " format, __PRETTY_FUNCTION__, __LINE__, ##args);

/**
 * @brief Initialize our logging system before first use
 * @details
 * This is for logging generated directly from this application.  Needs to be
 * called once before any logging occurs.
 *
 * @param verbosity The desired slog2 verbosity for the logs
 * @param bufferName Buffer name for slog2
 *
 * @return @c int which is EOK on success, error code on failure
 */
int loghInit(int verbosity, const char* bufferName);

/**
 * @brief Add a log entry at a given severity level
 * @details
 * Macros call this function to add a log entry; do not call directly, use the
 * macros instead.
 *
 * @param severity 0-7 defined severity levels from slog2
 * @format printf-style format string
 */
void loghEntry(int severity, const char* format, ...) __attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

#endif  // _LOG_HANDLER_H_
