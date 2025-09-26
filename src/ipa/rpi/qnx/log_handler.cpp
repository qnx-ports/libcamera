/*
 * Copyright (c) 2025 BlackBerry Limited. All Rights Reserved.
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
 * @file log_handler.c
 *
 * @brief Implements the logging for the application
 *
 */

#include "log_handler.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static const char* BUFFER_NAME_MAIN = "main";

// Global variable for use by log macros
static slog2_buffer_t slog2_buffer = NULL;

int loghInit(int verbosity, const char* bufferName)
{
    slog2_buffer_set_config_t buffer_config;

    // Create a own small (8 kB) buffer for logging
    buffer_config.buffer_set_name              = (char*) bufferName;
    buffer_config.num_buffers                  = 1;
    buffer_config.verbosity_level              = verbosity;
    buffer_config.buffer_config[0].buffer_name = (char*) BUFFER_NAME_MAIN;
    buffer_config.buffer_config[0].num_pages   = 8;
    // Register the Buffer Set
    if (slog2_register(&buffer_config, &slog2_buffer, 0) == 0) {
        LOG_INFO("Registered with slog2");
    } else {
        // We will not have any logging - logging to NULL buffer is harmless
        slog2_buffer = NULL;
        return EIO;
    }

    return EOK;
}

void loghEntry(int severity, const char* format, ...)
{
    va_list arg;
    // Preserve errno
    int ext_errno = errno;

    va_start(arg, format);
    vslog2f(slog2_buffer, 0, severity, format, arg);
    va_end(arg);

    errno = ext_errno;
}
