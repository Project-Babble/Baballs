// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#include "resample.h"
#include "runtime.h"
#include <math.h>
#include <stdckdint.h>
#include <stdio.h>
#include <string.h>

#define lengthof(_array) (sizeof(_array) / sizeof((_array)[0]))
#define endof(_array) (&(_array)[lengthof(_array)])

// TODO: gamma correction?

static inline float BabbleImage_sampleAt(const struct BabbleImage *const this, const float x, const float y) {
	const long long pixel[2] = {
		llroundf((this->imageTransform[0][0] * x + this->imageTransform[1][0] * y + this->imageTransform[2][0]) * (this->size[0] - 1)),
		llroundf((this->imageTransform[0][1] * x + this->imageTransform[1][1] * y + this->imageTransform[2][1]) * (this->size[1] - 1)),
	};
	if(pixel[0] < 0 || pixel[0] >= this->size[0] || pixel[1] < 0 || pixel[1] >= this->size[1])
		return 0;
	float result = 0;
	const size_t offset = pixel[1] * this->stride[1] + pixel[0] * this->stride[0];
	for(const struct BabbleImage_Plane *plane = this->planes; plane < endof(this->planes); ++plane)
		result += this->data[offset + plane->offset] * plane->weight;
	return result * (1 / 255.f);
}

bool BabbleImage_resampleTo(const struct BabbleImage *const this, float buffer_out[const], const size_t buffer_len, const uint32_t width, const uint32_t height) {
	if(this->size[0] == 0 || this->size[1] == 0 || width == 0 || height == 0 || buffer_len == 0)
		return false;
	const size_t buffer_stride = buffer_len / height;
	size_t last[2] = {~(size_t)0, ~(size_t)0}, lastPixel = ~(size_t)0;
	if(!ckd_mul(&last[0], this->stride[0], this->size[0] - 1) || !ckd_mul(&last[1], this->stride[1], this->size[1] - 1) || !ckd_add(&lastPixel, last[0], last[1]) || lastPixel >= this->data_len)
		return false;
	for(const struct BabbleImage_Plane *plane = this->planes; plane < endof(this->planes); ++plane)
		if(this->data_len - lastPixel <= plane->offset)
			return false;
	const float norm = 1 / (float)((height > width ? height : width) - 1);
	const float xoff = (width > height) ? (float)(width - height) * (.5f * norm) : 0;
	const float yoff = (height > width) ? (float)(height - width) * (.5f * norm) : 0;
	for(uint32_t y = 0; y < height; ++y)
		for(uint32_t x = 0; x < width; ++x)
			buffer_out[y * buffer_stride + x] = BabbleImage_sampleAt(this, (float)x * norm + xoff, (float)y * norm + yoff);
	return true;
}
