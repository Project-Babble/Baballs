// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>

enum VideoSource_Control : int16_t {
	VideoSource_Control_Mode, // uint

	VideoSource_Control_Brightness, // unorm
	VideoSource_Control_Contrast, // unorm
	VideoSource_Control_Saturation, // unorm
	VideoSource_Control_Hue, // snorm
	VideoSource_Control_Gamma, // unorm
	VideoSource_Control_Exposure, // Variant<unorm, ExposureMode>
	VideoSource_Control_Gain, // Nullable<uint>
	VideoSource_Control_Sharpness, // unorm

	VideoSource_Control_WhiteBalance, // Nullable<uint>
	VideoSource_Control_HFlip, // bool
	VideoSource_Control_VFlip, // bool
	VideoSource_Control_PowerLine, // Nullable<Frequency>
};

enum VideoFormat : uint32_t {
	VideoFormat_None,
	VideoFormat_Bayer,
	VideoFormat_Gray8,
	VideoFormat_Gray16LE,
	VideoFormat_Gray16BE,
	VideoFormat_BGRx,
	VideoFormat_YUY2,
	VideoFormat_NV12,
	VideoFormat_MJPEG,
	VideoFormat_H264,
	// non-exhaustive; may add more formats here if necessary
};

enum VideoSource_ExposureMode : int16_t {
	VideoSource_ExposureMode_Manual,
	VideoSource_ExposureMode_Auto = -1,
	VideoSource_ExposureMode_Shutter = -2,
	VideoSource_ExposureMode_Aperture = -3,
};

enum VideoSource_Frequency : int16_t {
	VideoSource_Frequency_None = -1,
	VideoSource_Frequency_50Hz,
	VideoSource_Frequency_60Hz,
};

struct VideoSource_Mode {
	enum VideoFormat format;
	struct {
		uint32_t width, height;
	} size;
	struct {
		uint32_t num, denom;
	} framerate;
};

enum VideoContext_SourceId : uint64_t {
	VideoContext_SourceId_None,
};

struct VideoSource;
struct VideoSource_Mode VideoSource_currentMode(struct VideoSource *this);
void VideoSource_onMode(struct VideoSource *this, void (*callback)(void *userptr, struct VideoSource_Mode mode), void *userptr);
void VideoSource_onFrame(struct VideoSource *this, void (*callback)(void *userptr, const uint8_t data[], uint32_t data_len, uint64_t timestamp), void *userptr);
void VideoSource_play(struct VideoSource *this, bool play);
void VideoSource_ref(struct VideoSource *this);
bool VideoSource_unref(struct VideoSource *this, void **onMode_out, void **onFrame_out);

// helpers for .NET wrapper
void **VideoSource_onMode_modifyUserptrLocked(struct VideoSource *this);
void **VideoSource_onFrame_modifyUserptrLocked(struct VideoSource *this);
void VideoSource_unlock(struct VideoSource *this);

struct VideoContext;
struct VideoContext *VideoContext_new(const char name[], uint32_t name_len, intptr_t *waitHandle_out);
struct VideoSource *VideoContext_open(struct VideoContext *this, enum VideoContext_SourceId id);
struct VideoSource *VideoContext_openPath(struct VideoContext *this, const char path[], uint32_t path_len);
void VideoContext_tick(struct VideoContext *this);
void VideoContext_ref(struct VideoContext *this);
bool VideoContext_unref(struct VideoContext *this);
