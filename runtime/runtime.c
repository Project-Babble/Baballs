// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#include "runtime.h"
#include <onnxruntime/onnxruntime_c_api.h>
#include <stdio.h>
#include <threads.h>
#define lengthof(_array) (sizeof(_array) / sizeof((_array)[0]))

struct Zone {
	OrtSession *session;
	OrtValue *input, *output[2]; // TODO: double buffer output so `BabbleRuntime_lockZones(wait: false)` doesn't block inference
	unsigned swap:1;
	uint32_t inputSize[2];
	char *inputName, *outputName;
	BabbleTimestamp timestamp, pendingTimestamp;
	BabbleZones outputSync;
};

struct BabbleRuntime {
	mtx_t mutex;
	cnd_t processFinished;
	const OrtApi *ort;
	struct Zone zones[BabbleZone_COUNT];
	BabbleZones pending, held;
	BabbleRuntime_OnData *onData;
	void *onData_userptr;
};

const char zoneNames[][9] = {
	"LeftEye",
	"RightEye",
	"Mouth",
};
static_assert(lengthof(zoneNames) == BabbleZone_COUNT);

static bool BabbleRuntime_lock(struct BabbleRuntime *const this) {
	if(this == nullptr)
		return false;
	if(mtx_lock(&this->mutex) != thrd_success) {
		fprintf(stderr, "mtx_lock() failed\n");
		abort();
	}
	return true;
}

static void BabbleRuntime_unlock(struct BabbleRuntime *const this) {
	if(mtx_unlock(&this->mutex) != thrd_success) {
		fprintf(stderr, "mtx_unlock() failed\n");
		abort();
	}
}

[[gnu::visibility("default")]] struct BabbleRuntime *BabbleRuntime_init() {
	const OrtApiBase *const ortBase = OrtGetApiBase();
	if(ortBase == nullptr) {
		fprintf(stderr, "OrtGetApiBase() failed\n");
		return nullptr;
	}
	const OrtApi *const ort = ortBase->GetApi(ORT_API_VERSION);
	if(ort == nullptr) {
		fprintf(stderr, "OrtApiBase::GetApi() failed\n");
		return nullptr;
	}
	struct BabbleRuntime *const this = calloc(1, sizeof(*this));
	if(this == nullptr)
		return nullptr;
	if(mtx_init(&this->mutex, mtx_plain | mtx_recursive) != thrd_success) {
		fprintf(stderr, "mtx_init() failed\n");
		goto fail0;
	}
	if(cnd_init(&this->processFinished) != thrd_success) {
		fprintf(stderr, "cnd_init() failed\n");
		goto fail1;
	}
	this->ort = ort;
	return this;
	fail1: mtx_destroy(&this->mutex);
	fail0: free(this);
	return nullptr;
}

// assumes lock
static void BabbleRuntime_wait(struct BabbleRuntime *const this, BabbleZones zones) {
	while((this->pending & zones) != 0) {
		if(cnd_wait(&this->processFinished, &this->mutex) != thrd_success) {
			fprintf(stderr, "cnd_wait() failed\n");
			abort();
		}
	}
}

[[gnu::visibility("default")]] void BabbleRuntime_free(struct BabbleRuntime *const this) {
	if(!BabbleRuntime_lock(this))
		return;
	BabbleRuntime_wait(this, (BabbleZones)~0u);
	BabbleRuntime_unlock(this);
	cnd_destroy(&this->processFinished);
	mtx_destroy(&this->mutex);
	free(this);
}

[[gnu::visibility("default")]] bool BabbleRuntime_loadModel(struct BabbleRuntime *const this, const uint8_t model[const], const size_t model_len, const BabbleZones zones) {
	/*this->ort->SetIntraOpNumThreads(session, 1);
	this->ort->SetGraphOptimizationLevel(session, ORT_ENABLE_BASIC); // TODO*/

	/*status = this->ort->SessionGetInputName(session, 0, allocator, &zone->inputName);
	status = this->ort->SessionGetOutputName(session, 0, allocator, &zone->outputName);*/

	/*OrtTensorTypeAndShapeInfo *shape = nullptr;
	int64_t inputSize[4] = {};
	status = this->ort->GetTensorTypeAndShape(zone->input, &shape);
	status = this->ort->GetDimensions(shape, inputSize, lengthof(inputSize));
	status = this->ort->ReleaseTensorTypeAndShapeInfo(shape);
	if(inputSize[0] != 1 || inputSize[1] != 1 || inputSize[2] < 1 || inputSize[3] < 1) {
		fprintf(stderr, "Failed to get input tensor size\n");
		return;
	}*/
	return false; // TODO
}

static void BabbleRuntime_onProcess(struct BabbleRuntime *const this, OrtValue **const outputs, const size_t num_outputs, const OrtStatusPtr status, const enum BabbleZone zone) {
	// TODO: wait for locked zones
	BabbleRuntime_lock(this);
	// TODO: write outputs
	this->zones[zone].timestamp = this->zones[zone].pendingTimestamp;
	this->held |= 1u << zone;
	if((this->held & this->zones[zone].outputSync) != this->zones[zone].outputSync)
		return;
	this->held &= ~this->zones[zone].outputSync;
	if(this->onData != nullptr)
		this->onData(this->onData_userptr, this, this->zones[zone].outputSync, this->zones[zone].timestamp);
	this->pending &= ~this->zones[zone].outputSync;
	if(cnd_broadcast(&this->processFinished) != thrd_success) {
		fprintf(stderr, "cnd_broadcast() failed\n");
		abort();
	}
	BabbleRuntime_unlock(this);
}

static void BabbleRuntime_onProcess_leftEye(void *const userptr, OrtValue **const outputs, const size_t num_outputs, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, num_outputs, status, BabbleZone_LeftEye);
}

static void BabbleRuntime_onProcess_rightEye(void *const userptr, OrtValue **const outputs, const size_t num_outputs, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, num_outputs, status, BabbleZone_RightEye);
}

static void BabbleRuntime_onProcess_mouth(void *const userptr, OrtValue **const outputs, const size_t num_outputs, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, num_outputs, status, BabbleZone_Mouth);
}

static bool BabbleImage_resampleTo(const struct BabbleImage *const this, float grayscale_out[const], size_t grayscale_len, uint32_t width) {
	return false; // TODO
}

[[gnu::visibility("default")]] bool BabbleRuntime_pushFrame(struct BabbleRuntime *const this, const struct BabbleImage images[const], const uint32_t images_len, const BabbleTimestamp timestamp) {
	static const RunAsyncCallbackFn onProcess[] = {
		BabbleRuntime_onProcess_leftEye,
		BabbleRuntime_onProcess_rightEye,
		BabbleRuntime_onProcess_mouth,
	};
	static_assert(lengthof(onProcess) == BabbleZone_COUNT);

	if(!BabbleRuntime_lock(this))
		return false;
	bool result = false;
	BabbleZones outputSync = 0, dispatched = 0;
	for(const struct BabbleImage *image = images; image < &images[images_len]; ++image) {
		if(image->zone >= lengthof(onProcess)) {
			fprintf(stderr, "Invalid zone '%u'\n", (unsigned)image->zone);
			goto unlock;
		}
		if(this->zones[image->zone].session == nullptr) {
			fprintf(stderr, "No ONNX model loaded for zone '%s'\n", zoneNames[image->zone]);
			goto unlock;
		}
		outputSync |= 1u << image->zone;
	}
	if(outputSync == 0) {
		result = true;
		goto unlock;
	}
	BabbleRuntime_wait(this, outputSync);
	for(const struct BabbleImage *image = images; image < &images[images_len]; ++image) {
		struct Zone *const zone = &this->zones[image->zone];
		zone->outputSync = outputSync;
		zone->pendingTimestamp = timestamp;
		void *inputData = nullptr;
		OrtStatus *status = this->ort->GetTensorMutableData(zone->input, &inputData);
		if(status != nullptr) {
			fprintf(stderr, "OrtApi::GetTensorMutableData() failed: %s\n", this->ort->GetErrorMessage(status));
			goto fail;
		}
		if(!BabbleImage_resampleTo(image, (float*)inputData, zone->inputSize[0] * zone->inputSize[1], zone->inputSize[0])) {
			fprintf(stderr, "BabbleImage_resampleTo() failed\n");
			goto fail;
		}
		status = this->ort->RunAsync(zone->session, nullptr, (const char*[1]){zone->inputName}, (const OrtValue*[1]){zone->input}, 1,
			(const char*[1]){zone->outputName}, 1, &zone->output[zone->swap], onProcess[image->zone], this);
		if(status != nullptr) {
			fprintf(stderr, "OrtApi::RunAsync() failed: %s\n", this->ort->GetErrorMessage(status));
			goto fail;
		}
		dispatched |= 1u << image->zone;
		continue;
		fail:
		if(status != nullptr)
			this->ort->ReleaseStatus(status);
		while(image-- > images)
			this->zones[image->zone].outputSync = dispatched;
		goto unlock;
	}
	result = true;
	unlock: BabbleRuntime_unlock(this);
	return true;
}

[[gnu::visibility("default")]] void BabbleRuntime_onData(struct BabbleRuntime *const this, BabbleRuntime_OnData *const onData, void *const onData_userptr) {
	if(!BabbleRuntime_lock(this))
		return;
	this->onData = onData;
	this->onData_userptr = onData_userptr;
	BabbleRuntime_unlock(this);
}

[[gnu::visibility("default")]] void BabbleRuntime_lockZones(struct BabbleRuntime *const this, const BabbleZones zones, const bool wait) {
	if(!BabbleRuntime_lock(this))
		return;
	if(wait)
		BabbleRuntime_wait(this, zones);
	BabbleRuntime_unlock(this);
	// TODO
}

[[gnu::visibility("default")]] BabbleTimestamp BabbleRuntime_getParams(struct BabbleRuntime *const this, const enum BabbleParam first, float params_out[const], const uint32_t params_len) {
	return BabbleTimestamp_Invalid; // TODO
}

[[gnu::visibility("default")]] BabbleTimestamp BabbleRuntime_getGazes(struct BabbleRuntime *const this, float (*const gazes_out)[2][4]) {
	return BabbleTimestamp_Invalid; // TODO
}
