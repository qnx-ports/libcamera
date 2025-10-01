/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019-2023, Raspberry Pi Ltd
 *
 * Raspberry Pi IPA
 *
 * Modified by BlackBerry Limited
 *
 * This file takes functions from the following files to
 * make algorithms usable for QNX:
 *
 * src/ipa/rpi/common/ipa_base.cpp
 * src/ipa/rpi/pisp/pisp.cpp
 * src/ipa/rpi/vc4/vc4.cpp
 */

#include "rpi_camera_ipa.h"

#include <errno.h>
#include <memory>

#include <log_handler.h>
#include <linux/bcm2835-isp.h>
#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include "pisp_statistics.h"

#include "cam_helper/cam_helper.h"
#include "controller/agc_algorithm.h"
#include "controller/agc_status.h"
#include "controller/awb_algorithm.h"
#include "controller/awb_status.h"
#include "controller/controller.h"

using namespace std::chrono_literals;
using namespace std::literals::chrono_literals;

// slog2 name
static const char* LOG_NAME =                                   "rpi_camera_ipa";

// Configure the sensor with these values initially.
constexpr double defaultAnalogueGain =                          1.0;
constexpr libcamera::utils::Duration defaultExposureTime =      20.0ms;
constexpr libcamera::utils::Duration defaultMinFrameDuration =  1.0s / 30.0;
constexpr libcamera::utils::Duration defaultMaxFrameDuration =  250.0s;

// imx708 sensor constants
constexpr uint16_t IMX708_MAX_SENSOR_WIDTH =                    4608u;
constexpr uint16_t IMX708_MAX_SENSOR_HEIGHT =                   2592u;
constexpr uint32_t IMX708_BITS_PER_PIXEL =                      10u;
constexpr uint32_t IMX708_MIN_GAIN_CODE =                       112u;
constexpr uint32_t IMX708_MAX_GAIN_CODE =                       960u;
constexpr uint32_t IMX708_DEFAULT_EXPOSURE_LINES =              0x640u;
constexpr uint32_t IMX708_DEFAULT_VBLANK =                      1198u;
constexpr uint32_t IMX708_DEFAULT_HBLANK =                      5520u;
constexpr uint64_t IMX708_PIXEL_RATE =                          585600000u;
constexpr uint64_t IMX708_MIN_FRAME_LENGTH =                    1336u;
constexpr uint64_t IMX708_MAX_FRAME_LENGTH =                    8388480u;
constexpr int IMX708_MIN_LINE_LENGTH =                          7824;
constexpr int IMX708_MAX_LINE_LENGTH =                          7824;
constexpr libcamera::utils::Duration IMX708_MAX_EXPOSURE_TIME = 32680.16us;

// Multiply by this amount for RPi4 red blue green gains
constexpr int32_t RPI4_WHITE_BALANCE_CONVERSION =               1000;

// Number of output int32_t field bits for floating point to int32_t conversion
constexpr size_t RPI5_WHITE_BALANCE_FIELD_BITS =                14;
// Number of output int32_t fractional bits for floating point to int32_t conversion
constexpr size_t RPI5_WHITE_BALANCE_FRACTIONAL_BITS =           10;

// Data structure to keep track of the library state
struct RpiIpaHandle_t {
    std::unique_ptr<RPiController::CamHelper> camHelper {};
    RPiController::Controller controller                {};
    RPiController::Metadata rpiMetadata                 {};
    rpiPlatform_t rpiPlatform                           {};
    CameraMode cameraMode                               {};
};

extern "C" {

/**
 * @brief Set initial camera mode with imx708 default values
 *
 * This modified function is taken from src/ipa/rpi/common/ipa_base.cpp
 *
 * In Linux, this function would read a generated sensor config file, but
 * we don't have that mechanism so hardcode imx708 values for now
 *
 * @param[in] handle Handle to the library
 * @param[in] width Width of the current viewfinder in pixels
 * @param[in] height Height of the current viewfinder in pixels
 */
static void setMode(RpiIpaHandle_t* handle, uint16_t width, uint16_t height)
{
    handle->cameraMode.bitdepth = IMX708_BITS_PER_PIXEL;
    handle->cameraMode.width = width;
    handle->cameraMode.height = height;
    handle->cameraMode.sensorWidth = IMX708_MAX_SENSOR_WIDTH;
    handle->cameraMode.sensorHeight = IMX708_MAX_SENSOR_HEIGHT;
    handle->cameraMode.cropX = 0;
    handle->cameraMode.cropY = 0;
    handle->cameraMode.pixelRate = IMX708_PIXEL_RATE;

    /*
     * Calculate scaling parameters. The scale_[xy] factors are determined
     * by the ratio between the crop rectangle size and the output size.
     */
    handle->cameraMode.scaleX = (double) IMX708_MAX_SENSOR_WIDTH / width;
    handle->cameraMode.scaleY = (double) IMX708_MAX_SENSOR_HEIGHT / height;

    /*
     * We're not told by the pipeline handler how scaling is split between
     * binning and digital scaling. For now, as a heuristic, assume that
     * downscaling up to 2 is achieved through binning, and that any
     * additional scaling is achieved through digital scaling.
     *
     * \todo Get the pipeline handle to provide the full data
     */
    handle->cameraMode.binX = std::min(2, static_cast<int>(handle->cameraMode.scaleX));
    handle->cameraMode.binY = std::min(2, static_cast<int>(handle->cameraMode.scaleY));

    /* The noise factor is the square root of the total binning factor. */
    handle->cameraMode.noiseFactor = std::sqrt(handle->cameraMode.binX * handle->cameraMode.binY);

    /*
     * Calculate the line length as the ratio between the line length in
     * pixels and the pixel rate.
     */
    handle->cameraMode.minLineLength = IMX708_MIN_LINE_LENGTH * (1.0s / IMX708_PIXEL_RATE);
    handle->cameraMode.maxLineLength = IMX708_MAX_LINE_LENGTH * (1.0s / IMX708_PIXEL_RATE);

    /*
     * Ensure that the maximum pixel processing rate does not exceed the ISP
     * hardware capabilities. If it does, try adjusting the minimum line
     * length to compensate if possible.
     */
    libcamera::utils::Duration minPixelTime = handle->controller.getHardwareConfig().minPixelProcessingTime;
    libcamera::utils::Duration pixelTime = handle->cameraMode.minLineLength / handle->cameraMode.width;
    if (minPixelTime && pixelTime < minPixelTime) {
        libcamera::utils::Duration adjustedLineLength = minPixelTime * handle->cameraMode.width;
        if (adjustedLineLength <= handle->cameraMode.maxLineLength) {
            LOG_INFO("Adjusting mode minimum line length from  %d to %d because of ISP constraints", handle->cameraMode.minLineLength, adjustedLineLength);
            handle->cameraMode.minLineLength = adjustedLineLength;
        } else {
            LOG_ERROR("Sensor minimum line length of %d is below the minimum allowable ISP limit of %d MPix/s", pixelTime * handle->cameraMode.width, adjustedLineLength);
            LOG_ERROR("THIS WILL CAUSE IMAGE CORRUPTION!!! Please update the camera sensor driver to allow more horizontal blanking control.");
        }
    }

    /*
     * Set the frame length limits for the mode to ensure exposure and
     * framerate calculations are clipped appropriately.
     */
    handle->cameraMode.minFrameLength = IMX708_MIN_FRAME_LENGTH;
    handle->cameraMode.maxFrameLength = IMX708_MAX_FRAME_LENGTH;

    /* Store these for convenience. */
    handle->cameraMode.minFrameDuration = handle->cameraMode.minFrameLength * handle->cameraMode.minLineLength;
    handle->cameraMode.maxFrameDuration = handle->cameraMode.maxFrameLength * handle->cameraMode.maxLineLength;

    /*
     * Some sensors may have different sensitivities in different modes;
     * the CamHelper will know the correct value.
     */
    handle->cameraMode.sensitivity = handle->camHelper->getModeSensitivity(handle->cameraMode);

    handle->cameraMode.minAnalogueGain = handle->camHelper->gain(IMX708_MIN_GAIN_CODE);
    handle->cameraMode.maxAnalogueGain = handle->camHelper->gain(IMX708_MAX_GAIN_CODE);

    /*
     * We need to give the helper the min/max frame durations so it can calculate
     * the correct exposure limits below.
     */
    handle->camHelper->setCameraMode(handle->cameraMode);

    /*
     * Exposure time is calculated based on the limits of the frame
     * durations.
     */
    handle->cameraMode.minExposureTime = handle->camHelper->exposure(1,
                          handle->cameraMode.minLineLength);
    handle->cameraMode.maxExposureTime = libcamera::utils::Duration::max();
    handle->camHelper->getBlanking(handle->cameraMode.maxExposureTime, handle->cameraMode.minFrameDuration,
                 handle->cameraMode.maxFrameDuration);
}

/**
 * @brief Helper function for filling RPiController::Metadata with
 * sensor default values
 *
 * This modified function is taken from src/ipa/rpi/common/ipa_base.cpp
 *
 * In Linux, this function uses v4l2 to get the current imx708 sensor
 * values. Set default values for now for QNX.
 *
 * @param[in] handle Handle to the library
 * @param[in,out] rpiMetadata Fill this variable with default imx708 values
 *
 * @return Return EOK on sucess, otherwise an error
 */
static int fillDeviceStatus(RpiIpaHandle_t* handle, RPiController::Metadata& rpiMetadata)
{
    // Sanity check
    if (handle == NULL) {
        LOG_ERROR("Invalid handle");
        return EINVAL;
    }

    DeviceStatus deviceStatus = {};

    // Set to default imx708 values
    int32_t exposureLines = IMX708_DEFAULT_EXPOSURE_LINES;
    int32_t gainCode = IMX708_MIN_GAIN_CODE;
    int32_t vblank = IMX708_DEFAULT_VBLANK;
    int32_t hblank = IMX708_DEFAULT_HBLANK;

    deviceStatus.lineLength = handle->camHelper->hblankToLineLength(hblank);
    deviceStatus.exposureTime = handle->camHelper->exposure(exposureLines, deviceStatus.lineLength);
    deviceStatus.analogueGain = handle->camHelper->gain(gainCode);
    deviceStatus.frameLength = handle->cameraMode.height + vblank;

    rpiMetadata.set("device.status", deviceStatus);

    return EOK;
}

/**
 * @brief Helper function for converting a floating point to
 * a fixed point integer
 *
 * This function is taken from src/ipa/rpi/pisp/pisp.cpp
 *
 * @param[in] value Floating point to convert
 * @param[in] fieldBits Total number of bits for storing the int32_t integer
 * @param[in] fracBits Number of fractional bits
 * @param[in] isSigned @c value's sign
 * @param[in] desc Optional description string
 *
 * @return Return the converted int32_t value
 */
static int32_t clampField(double value,
                          std::size_t fieldBits,
                          std::size_t fracBits = 0,
                          bool isSigned = false,
                          const char *desc = nullptr)
{
    ASSERT(fracBits <= fieldBits && fieldBits <= 32);

    int min = -(isSigned << (fieldBits - 1));
    int max = (1 << (fieldBits - isSigned)) - 1;
    int32_t val =
    std::clamp<int32_t>(std::round(value * (1 << fracBits)), min, max);

    if (desc && val / (1 << fracBits) != value) {
        LOG_WARNING("rounded/clamped to %f", val / (1 << fracBits));
    }

    return val;
}

/**
 * @brief Helper function for parsing RPi4 ISP metadata
 *
 * This modified function is taken from src/ipa/rpi/vc4/vc4.cpp
 *
 * @param[in] handle Handle to the library
 * @param[in] mem ISP metadata buffer
 *
 * @return Return @c RPiController::StatisticsPtr containing parsed ISP metadata
 */
static RPiController::StatisticsPtr rpi4ProcessStats(RpiIpaHandle_t* handle, const libcamera::Span<uint8_t>& mem)
{
    // Sanity check
    if (handle == NULL) {
        LOG_ERROR("NULL handle");
        return NULL;
    }

    const bcm2835_isp_stats *stats = reinterpret_cast<bcm2835_isp_stats *>(mem.data());
    RPiController::StatisticsPtr statistics = new (std::nothrow) RPiController::Statistics( RPiController::Statistics::AgcStatsPos::PreWb,
                                 RPiController::Statistics::ColourStatsPos::PostLsc);
    const RPiController::Controller::HardwareConfig &hw = handle->controller.getHardwareConfig();
    unsigned int i;

    /* RGB histograms are not used, so do not populate them. */
    statistics->yHist = RPiController::Histogram(stats->hist[0].g_hist,
                             hw.numHistogramBins);

    /* All region sums are based on a 16-bit normalised pipeline bit-depth. */
    unsigned int scale =  RPiController::Statistics::NormalisationFactorPow2 - hw.pipelineWidth;

    statistics->awbRegions.init(hw.awbRegions);
    for (i = 0; i < statistics->awbRegions.numRegions(); i++)
        statistics->awbRegions.set(i, { { stats->awb_stats[i].r_sum << scale,
                          stats->awb_stats[i].g_sum << scale,
                          stats->awb_stats[i].b_sum << scale },
                        stats->awb_stats[i].counted,
                        stats->awb_stats[i].notcounted });

    RPiController::AgcAlgorithm *agc = dynamic_cast<RPiController::AgcAlgorithm *>(
        handle->controller.getAlgorithm("agc"));
    if (!agc) {
        LOG_ERROR("No AGC algorithm - not copying statistics");
        statistics->agcRegions.init(0);
    } else {
        statistics->agcRegions.init(hw.agcRegions);
        const std::vector<double, NothrowAllocator<double>> &weights = agc->getWeights();
        for (i = 0; i < statistics->agcRegions.numRegions(); i++) {
            uint64_t rSum = (stats->agc_stats[i].r_sum << scale) * weights[i];
            uint64_t gSum = (stats->agc_stats[i].g_sum << scale) * weights[i];
            uint64_t bSum = (stats->agc_stats[i].b_sum << scale) * weights[i];
            uint32_t counted = stats->agc_stats[i].counted * weights[i];
            uint32_t notcounted = stats->agc_stats[i].notcounted * weights[i];
            statistics->agcRegions.set(i, { { rSum, gSum, bSum },
                            counted,
                            notcounted });
        }
    }

    statistics->focusRegions.init(hw.focusRegions);
    for (i = 0; i < statistics->focusRegions.numRegions(); i++)
        statistics->focusRegions.set(i, { stats->focus_stats[i].contrast_val[1][1] / 1000,
                          stats->focus_stats[i].contrast_val_num[1][1],
                          stats->focus_stats[i].contrast_val_num[1][0] });

    return statistics;
}

/**
 * @brief Helper function for parsing RPi5 ISP metadata
 *
 * This modified function is taken from src/ipa/rpi/pisp/pisp.cpp
 *
 * @param[in] handle Handle to the library
 * @param[in] mem ISP metadata buffer
 *
 * @return Return @c RPiController::StatisticsPtr containing parsed ISP metadata
 */
static RPiController::StatisticsPtr rpi5ProcessStats(RpiIpaHandle_t* handle, const libcamera::Span<uint8_t>& mem)
{
    // Sanity check
    if (handle == NULL) {
        LOG_ERROR("NULL handle");
        return NULL;
    }

    const pisp_statistics *stats = reinterpret_cast<pisp_statistics *>(mem.data());

    RPiController::AgcAlgorithm *agc = dynamic_cast<RPiController::AgcAlgorithm *>(
        handle->controller.getAlgorithm("agc"));
    agc->setMeteringMode("matrix");
    agc->setEv(0, 1.0);

    unsigned int i;
    RPiController::StatisticsPtr statistics =
        new (std::nothrow) RPiController::Statistics(RPiController::Statistics::AgcStatsPos::PostWb,
                                                     RPiController::Statistics::ColourStatsPos::PreLsc);

    /* RGB histograms are not used, so do not populate them. */
    statistics->yHist = RPiController::Histogram(stats->agc.histogram,
                             PISP_AGC_STATS_NUM_BINS);

    statistics->awbRegions.init({ PISP_AWB_STATS_SIZE, PISP_AWB_STATS_SIZE });
    for (i = 0; i < statistics->awbRegions.numRegions(); i++)
        statistics->awbRegions.set(i, { { stats->awb.zones[i].R_sum,
                          stats->awb.zones[i].G_sum,
                          stats->awb.zones[i].B_sum },
                        stats->awb.zones[i].counted, 0 });

    /* AGC region sums only get collected on floating zones. */
    statistics->agcRegions.init({ 0, 0 }, PISP_FLOATING_STATS_NUM_ZONES);
    for (i = 0; i < statistics->agcRegions.numRegions(); i++)
        statistics->agcRegions.setFloating(i,
                           { { 0, 0, 0, stats->agc.floating[i].Y_sum },
                             stats->agc.floating[i].counted, 0 });

    statistics->focusRegions.init({ PISP_CDAF_STATS_SIZE, PISP_CDAF_STATS_SIZE });
    for (i = 0; i < statistics->focusRegions.numRegions(); i++)
        statistics->focusRegions.set(i, { stats->cdaf.foms[i] >> 20, 0, 0 });

    return statistics;
}

RpiIpaHandle_t* rpiIpaStart(rpiPlatform_t platform,
                            const char* configPath,
                            const char* sensorName,
                            uint16_t width,
                            uint16_t height)
{
    int err;

    // Initialize logging
    err = loghInit(SLOG2_INFO, LOG_NAME);
    if (err != EOK) {
        (void) fprintf(stderr, "Failed to initialize logging\n");
        return NULL;
    }

    RpiIpaHandle_t* handle = new (std::nothrow) RpiIpaHandle_t();

    // This library only supports RPi4 and RPi5
    if ((platform == rpiPlatform_t::RPI4) ||
        (platform == rpiPlatform_t::RPI5)) {
        handle->rpiPlatform = platform;
    } else {
        LOG_ERROR("Invalid platform: %d", platform);
        delete handle;
        return NULL;
    }

    // Create a camera helper
    handle->camHelper = std::unique_ptr<RPiController::CamHelper>(RPiController::CamHelper::create(sensorName));
    if (handle->camHelper == NULL) {
        LOG_ERROR("Failed to create a camera helper for %s", sensorName);
        delete handle;
        return NULL;
    }

    // Configure algorithms with the given configuration file
    err = handle->controller.read(configPath);
    if (err != EOK) {
        LOG_ERROR("Failed to load tuning data file %s: err = %d", configPath, err);
        delete handle;
        return NULL;
    }

    // Initialize algorithms
    handle->controller.initialise();
    handle->camHelper->setHwConfig(handle->controller.getHardwareConfig());

    // Set camera mode
    setMode(handle, width, height);
    handle->camHelper->setCameraMode(handle->cameraMode);

    // Clear metadata and fill with default values
    handle->rpiMetadata.clear();
    err = fillDeviceStatus(handle, handle->rpiMetadata);
    if (err != EOK) {
        LOG_ERROR("Failed to fill device status: err = %d", err);
        return NULL;
    }

    // Switch algorithm mode according to the parsed metadata
    handle->controller.switchMode(handle->cameraMode, &handle->rpiMetadata);

    // Set auto exposure algorithm settings
    RPiController::AgcAlgorithm *agc = dynamic_cast<RPiController::AgcAlgorithm *>(
    handle->controller.getAlgorithm("agc"));
    if (agc == NULL) {
        LOG_ERROR("Failed to get agc algorithm");
        return NULL;
    }
    agc->setMaxExposureTime(IMX708_MAX_EXPOSURE_TIME);
    agc->setEv(0, 1);

    return handle;
}

void rpiIpaStop(RpiIpaHandle_t* handle)
{
    delete handle;
}

int rpiIpaProcessData(RpiIpaHandle_t* handle,
                      void* sensorMetadata,
                      size_t sensorMetadataSize,
                      void* ispMetadata,
                      size_t ispMetadataSize,
                      int32_t* exposureTime,
                      int32_t* iso,
                      int32_t* gainR,
                      int32_t* gainG,
                      int32_t* gainB)
{
    RPiController::StatisticsPtr statistics;

    // Sanity check
    if ((handle == NULL) ||
        (sensorMetadata == NULL) ||
        (ispMetadata == NULL) ||
        (exposureTime == NULL) ||
        (iso == NULL) ||
        (gainR == NULL) ||
        (gainG == NULL) ||
        (gainB == NULL)) {
        LOG_ERROR("NULL parameter");
        return EINVAL;
    }

    libcamera::Span<uint8_t> sensorMem((uint8_t*)sensorMetadata, sensorMetadataSize);
    libcamera::Span<uint8_t> ispMem((uint8_t*)ispMetadata, ispMetadataSize);

    // Parse sensor embedded metadata
    handle->camHelper->prepare(sensorMem, handle->rpiMetadata);
    // Configure algorithms with the parsed sensor metadata
    handle->controller.prepare(&handle->rpiMetadata);

    // Parse ISP metadata
    if (handle->rpiPlatform == RPI4) {
        statistics = rpi4ProcessStats(handle, ispMem);
    } else if (handle->rpiPlatform == RPI5) {
        statistics = rpi5ProcessStats(handle, ispMem);
    } else {
        LOG_ERROR("Invalid RPi platform %d", handle->rpiPlatform);
        return EINVAL;
    }

    // Make sure rpi*ProcessStats function has worked
    if (statistics == NULL) {
        LOG_ERROR("Failed to get parsed ISP metadata statistics");
        return EINVAL;
    }

    // Sensor specific configuration with ISP metadata
    handle->camHelper->process(statistics, handle->rpiMetadata);
    // Run algorithms with the parsed ISP metadata
    handle->controller.process(statistics, &handle->rpiMetadata);

    // Get the auto exposure algorithm output
    struct AgcStatus agcStatus;
    if (handle->rpiMetadata.get("agc.status", agcStatus) != 0) {
        delete statistics;
        return EINVAL;
    }

    // Get the auto whitebalance algorithm output
    struct AwbStatus awbStatus;
    if (handle->rpiMetadata.get("awb.status", awbStatus) != 0) {
        delete statistics;
        return EINVAL;
    }

    // Convert auto whitebalance output for each platform
    if (handle->rpiPlatform == RPI4) {
        *gainR = awbStatus.gainR * RPI4_WHITE_BALANCE_CONVERSION;
        *gainG = awbStatus.gainG * RPI4_WHITE_BALANCE_CONVERSION;
        *gainB = awbStatus.gainB * RPI4_WHITE_BALANCE_CONVERSION;
    } else if (handle->rpiPlatform == RPI5) {
        *gainR = clampField(awbStatus.gainR,
                            RPI5_WHITE_BALANCE_FIELD_BITS,
                            RPI5_WHITE_BALANCE_FRACTIONAL_BITS);
        *gainG = clampField(awbStatus.gainG,
                            RPI5_WHITE_BALANCE_FIELD_BITS,
                            RPI5_WHITE_BALANCE_FRACTIONAL_BITS);
        *gainB = clampField(awbStatus.gainB,
                            RPI5_WHITE_BALANCE_FIELD_BITS,
                            RPI5_WHITE_BALANCE_FRACTIONAL_BITS);
    } else {
        LOG_ERROR("Invalid RPi platform %d", handle->rpiPlatform);
        delete statistics;
        return EINVAL;
    }

    const int32_t minGainCode = handle->camHelper->gainCode(handle->cameraMode.minAnalogueGain);
    const int32_t maxGainCode = handle->camHelper->gainCode(handle->cameraMode.maxAnalogueGain);
    // Get the int32_t gainCode from agcStatus.analogueGain floating point
    int32_t gainCode = handle->camHelper->gainCode(agcStatus.analogueGain);

    /*
     * Ensure anything larger than the max gain code will not be passed to
     * DelayedControls. The AGC will correctly handle a lower gain returned
     * by the sensor, provided it knows the actual gain used.
     */
    gainCode = std::clamp<int32_t>(gainCode, minGainCode, maxGainCode);
    /* getBlanking might clip exposure time to the fps limits. */
    libcamera::utils::Duration exposure = agcStatus.exposureTime;
    auto [vblank, hblank] = handle->camHelper->getBlanking(exposure, handle->cameraMode.minFrameDuration,
                 handle->cameraMode.maxFrameDuration);
    int32_t exposureLines = handle->camHelper->exposureLines(exposure,
                       handle->camHelper->hblankToLineLength(hblank));

    // Return auto exposure algorithm outputs
    *exposureTime = exposureLines;
    *iso = gainCode;

    delete statistics;

    return EOK;
}

} // extern "C"
