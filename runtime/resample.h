// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>
#include <stddef.h>

struct BabbleImage;
bool BabbleImage_resampleTo(const struct BabbleImage *this_, float buffer_out[], size_t buffer_len, uint32_t width, uint32_t height);
