// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int64_t BabbleTimestamp;
const BabbleTimestamp BabbleTimestamp_Invalid = -1;

typedef uint8_t BabbleZones;
enum BabbleZone : BabbleZones {
	BabbleZone_LeftEye,
	BabbleZone_RightEye,
	BabbleZone_Mouth,
	BabbleZone_COUNT,
};

enum BabbleZones : BabbleZones {
	BabbleZones_LeftEye = 1u << BabbleZone_LeftEye,
	BabbleZones_RightEye = 1u << BabbleZone_RightEye,
	BabbleZones_Mouth = 1u << BabbleZone_Mouth,
};

// TODO: EyeSquint, EyeWide, EyeDilation, EyeConstrict
// TODO?: BrowPinch, BrowLowerer, BrowInnerUp, BrowOuterUp


enum BabbleParam {
	// BabbleZone_LeftEye
	BabbleParam_EyeLookOutLeft,
	BabbleParam_EyeLookInLeft,
	BabbleParam_EyeLookUpLeft,
	BabbleParam_EyeLookDownLeft,
	BabbleParam_EyeClosedLeft,

	// BabbleZone_RightEye
	BabbleParam_EyeLookOutRight,
	BabbleParam_EyeLookInRight,
	BabbleParam_EyeLookUpRight,
	BabbleParam_EyeLookDownRight,
	BabbleParam_EyeClosedRight,

	// BabbleZone_Mouth
	BabbleParam_CheekPuffLeft,
	BabbleParam_CheekPuffRight,
	BabbleParam_CheekSuckLeft,
	BabbleParam_CheekSuckRight,
	BabbleParam_JawOpen,
	BabbleParam_JawForward,
	BabbleParam_JawLeft,
	BabbleParam_JawRight,
	BabbleParam_NoseSneerLeft,
	BabbleParam_NoseSneerRight,
	BabbleParam_MouthFunnel,
	BabbleParam_MouthPucker,
	BabbleParam_MouthLeft,
	BabbleParam_MouthRight,
	BabbleParam_MouthRollUpper,
	BabbleParam_MouthRollLower,
	BabbleParam_MouthShrugUpper,
	BabbleParam_MouthShrugLower,
	BabbleParam_MouthClose,
	BabbleParam_MouthSmileLeft,
	BabbleParam_MouthSmileRight,
	BabbleParam_MouthFrownLeft,
	BabbleParam_MouthFrownRight,
	BabbleParam_MouthDimpleLeft,
	BabbleParam_MouthDimpleRight,
	BabbleParam_MouthUpperUpLeft,
	BabbleParam_MouthUpperUpRight,
	BabbleParam_MouthLowerDownLeft,
	BabbleParam_MouthLowerDownRight,
	BabbleParam_MouthPressLeft,
	BabbleParam_MouthPressRight,
	BabbleParam_MouthStretchLeft,
	BabbleParam_MouthStretchRight,
	BabbleParam_TongueOut,
	BabbleParam_TongueUp,
	BabbleParam_TongueDown,
	BabbleParam_TongueLeft,
	BabbleParam_TongueRight,
	BabbleParam_TongueRoll,
	BabbleParam_TongueBendDown,
	BabbleParam_TongueCurlUp,
	BabbleParam_TongueSquish,
	BabbleParam_TongueFlat,
	BabbleParam_TongueTwistLeft,
	BabbleParam_TongueTwistRight,

	BabbleParam_COUNT,
};

struct BabbleImage {
	const uint8_t *data;
	size_t data_len;
	uint32_t size[2];
	size_t stride[2];
	size_t planeOffsets[4];
	float colorTransform[4];
	float imageTransform[3][2];
	enum BabbleZone zone;
};

// TODO: convergence hint for gaze emulation when using single eye camera (may depend on XR_KHR_composition_layer_depth)
// TODO: GPU frame support?

struct BabbleRuntime *BabbleRuntime_init();
void BabbleRuntime_free(struct BabbleRuntime *this);
bool BabbleRuntime_loadModel(struct BabbleRuntime *this, const uint8_t model[], size_t model_len, BabbleZones zones); // TODO: split model and weights? precompilation?
bool BabbleRuntime_pushFrame(struct BabbleRuntime *this, const struct BabbleImage images[], uint32_t images_len, BabbleTimestamp timestamp);
typedef void BabbleRuntime_OnData(void *userptr, struct BabbleRuntime *runtime, BabbleZones zones, BabbleTimestamp timestamp);
void BabbleRuntime_onData(struct BabbleRuntime *this, BabbleRuntime_OnData *onData, void *onData_userptr); // data is locked for duration of callback
void BabbleRuntime_lockZones(struct BabbleRuntime *this, BabbleZones zones, bool wait); // ensures `BabbleRuntime_getParams()` and `BabbleRuntime_getGazes()` return fast, and prevents data from updating between calls
BabbleTimestamp BabbleRuntime_getParams(struct BabbleRuntime *this, enum BabbleParam first, float params_out[], uint32_t params_len);
BabbleTimestamp BabbleRuntime_getGazes(struct BabbleRuntime *this, float (*gazes_out)[2][4]);
