/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019-2023, Raspberry Pi Ltd
 *
 * Raspberry Pi IPA
 *
 * Modified by BlackBerry Limited
 */

#ifndef RPI_CAMERA_IPA
#define RPI_CAMERA_IPA

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RPI_NONE = 0,
    RPI4,
    RPI5,
} rpiPlatform_t;

// Forward declaration
struct RpiIpaHandle_t;

/**
 * @brief Start the image processing algorithm library
 *
 * Make sure to call this function before calling @c rpiIpaProcessData
 *
 * @param[in] platform Enum indicating which @c rpiPlatform_t it is
 * @param[in] configPath Absolute path to an algorithm config file
 * @param[in] sensorName Name of sensorName
 * @param[in] width Width of the current viewfinder in pixels
 * @param[in] height Height of the current viewfinder in pixels
 *
 * @return Return a handle to the image processing algorithm library on
 * success, otherwise a NULL
 */
RpiIpaHandle_t* rpiIpaStart(rpiPlatform_t platform,
                            const char* configPath,
                            const char* sensorName,
                            uint16_t width,
                            uint16_t height);

/**
 * @brief Clean up the image processing algorithm library
 *
 * @param[in] handle Handle to the image processing algorithm library
 */
void rpiIpaStop(RpiIpaHandle_t* handle);

/**
 * @brief Feed sensor embedded metadata and ISP metadata into the algorithms
 *
 * @param[in] handle Handle to the library
 * @param[in] sensorMetadata Buffer containing sensor embedded metadata
 * @param[in] sensorMetadataSize Size of @c sensorMetadata in bytes
 * @param[in] ispMetadata Buffer containing ISP metadata
 * @param[in] ispMetadataSize Size of @c ispMetadata in bytes
 * @param[out] exposureTime Output exposure time to set on the sensor
 * @param[out] iso Output iso to set on the sensor
 *
 * @return Return EOK on success, otherwise an error
 */
int rpiIpaProcessData(RpiIpaHandle_t* handle,
                      void* sensorMetadata,
                      size_t sensorMetadataSize,
                      void* ispMetadata,
                      size_t ispMetadataSize,
                      int32_t* exposureTime,
                      int32_t* iso);

#ifdef __cplusplus
}
#endif

#endif // RPI_CAMERA_IPA
