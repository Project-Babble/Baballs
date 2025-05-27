// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#include "runtime.h"
#include "resample.h"
#include <inttypes.h>
#include <onnxruntime/onnxruntime_c_api.h>
#include <stdio.h>
#include <threads.h>

[[maybe_unused]] static inline void *drop_ptr_(void *const ptr) {void *const inner = *(void**)ptr; *(void**)ptr = nullptr; return inner;}
#define drop_ptr(_ptr) ((typeof(*(_ptr))){drop_ptr_(_ptr)})
#define lengthof(_array) (sizeof(_array) / sizeof((_array)[0]))
#define endof(_array) (&(_array)[lengthof(_array)])
#define CHECK_ORT(_fail, _name, _ort, ...) { \
	OrtStatus *_status = ((_ort)->__VA_ARGS__); \
	if(_status != nullptr) { \
		fprintf(stderr, _name " failed: %s\n", (_ort)->GetErrorMessage(_status)); \
		(_ort)->ReleaseStatus(drop_ptr(&_status)); \
		_fail; \
	} \
}

struct ZoneContext {
	OrtSession *session; // may be shared across multiple contexts
	uint16_t inputSize[2];
	OrtValue *input, *output[2];
	char *inputName, *outputName;
	BabbleTimestamp timestamp[2];
	BabbleZones group;
	bool swap;
};

struct BabbleRuntime {
	mtx_t mutex;
	cnd_t processFinished, swapFinished;
	const OrtApi *ort;
	OrtSessionOptions *options;
	BabbleZones pending, pendingSwap, locked;
	BabbleRuntime_OnData *onData;
	void *onData_userptr;
	struct ZoneContext contexts[BabbleZone_COUNT];
};

static void mtx_lock_checked(mtx_t *const mutex) {
	if(mtx_lock(mutex) != thrd_success) {
		fprintf(stderr, "mtx_lock() failed\n");
		abort();
	}
}

static void mtx_unlock_checked(mtx_t *const mutex) {
	if(mtx_unlock(mutex) != thrd_success) {
		fprintf(stderr, "mtx_unlock() failed\n");
		abort();
	}
}

static inline enum BabbleZone BabbleZones_first(const BabbleZones zones) {
	return __builtin_ctz(zones | 1u << BabbleZone_COUNT);
}

static inline enum BabbleZone BabbleZones_next(const BabbleZones zones, const enum BabbleZone i) {
	return __builtin_ctz((zones & (~1u << i)) | 1u << BabbleZone_COUNT);
}

static void BabbleRuntime_cleanupContext(struct BabbleRuntime *const this, const enum BabbleZone zone) {
	struct ZoneContext *const context = &this->contexts[zone];
	if(context->session == nullptr)
		return;
	for(OrtValue **output = context->output; output < endof(context->output); ++output)
		this->ort->ReleaseValue(drop_ptr(output));
	this->ort->ReleaseValue(drop_ptr(&context->input));
	OrtSession *session = drop_ptr(&context->session);
	char *inputName = drop_ptr(&context->inputName), *outputName = drop_ptr(&context->outputName);
	*context = (struct ZoneContext){};
	for(enum BabbleZone i = 0; i < lengthof(this->contexts); ++i)
		if(i != zone && this->contexts[i].session == session)
			return;
	this->ort->ReleaseSession(drop_ptr(&session));
	OrtAllocator *allocator = nullptr;
	CHECK_ORT(return, "OrtApi::GetAllocatorWithDefaultOptions()", this->ort, GetAllocatorWithDefaultOptions(&allocator));
	CHECK_ORT(, "OrtApi::AllocatorFree()", this->ort, AllocatorFree(allocator, drop_ptr(&inputName)));
	CHECK_ORT(, "OrtApi::AllocatorFree()", this->ort, AllocatorFree(allocator, drop_ptr(&outputName)));
}

struct BabbleRuntime *BabbleRuntime_init() {
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
	struct BabbleRuntime *this = calloc(1, sizeof(*this));
	if(this == nullptr)
		return nullptr;
	this->ort = ort;
	CHECK_ORT(goto fail0, "OrtApi::CreateSessionOptions()", ort, CreateSessionOptions(&this->options));
	CHECK_ORT(goto fail1, "OrtApi::SetSessionGraphOptimizationLevel()", ort, SetSessionGraphOptimizationLevel(this->options, ORT_ENABLE_BASIC));
	CHECK_ORT(goto fail1, "OrtApi::SetIntraOpNumThreads()", ort, SetIntraOpNumThreads(this->options, 1));
	if(mtx_init(&this->mutex, mtx_plain | mtx_recursive) != thrd_success) {
		fprintf(stderr, "mtx_init() failed\n");
		goto fail1;
	}
	if(cnd_init(&this->processFinished) != thrd_success) {
		fprintf(stderr, "cnd_init() failed\n");
		goto fail2;
	}
	if(cnd_init(&this->swapFinished) != thrd_success) {
		fprintf(stderr, "cnd_init() failed\n");
		goto fail3;
	}
	return this;
	fail3: cnd_destroy(&this->processFinished);
	fail2: mtx_destroy(&this->mutex);
	fail1: ort->ReleaseSessionOptions(drop_ptr(&this->options));
	fail0: free(drop_ptr(&this));
	return nullptr;
}

static void BabbleRuntime_waitLocked(struct BabbleRuntime *const this, const BabbleZones zones, bool swap) {
	while((this->pending & zones) != 0) {
		if(cnd_wait(&this->processFinished, &this->mutex) != thrd_success) {
			fprintf(stderr, "cnd_wait() failed\n");
			abort();
		}
	}
	if(!swap)
		return;
	while((this->pendingSwap & zones) != 0) {
		if(cnd_wait(&this->swapFinished, &this->mutex) != thrd_success) {
			fprintf(stderr, "cnd_wait() failed\n");
			abort();
		}
	}
}

void BabbleRuntime_free(struct BabbleRuntime *this) {
	if(this == nullptr)
		return;
	mtx_lock_checked(&this->mutex);
	BabbleRuntime_waitLocked(this, (BabbleZones)~0u, false);
	mtx_unlock_checked(&this->mutex);
	for(enum BabbleZone i = 0; i < lengthof(this->contexts); ++i)
		BabbleRuntime_cleanupContext(this, i);
	cnd_destroy(&this->swapFinished);
	cnd_destroy(&this->processFinished);
	mtx_destroy(&this->mutex);
	this->ort->ReleaseSessionOptions(drop_ptr(&this->options));
	free(drop_ptr(&this));
}

bool BabbleRuntime_loadModel(struct BabbleRuntime *const this, const uint8_t model[const], const size_t model_len, const BabbleZones zones) {
	const enum BabbleZone first = BabbleZones_first(zones);
	if(this == nullptr || first >= lengthof(this->contexts))
		return false;
	const bool sharedEyeModel = (zones == (BabbleZone_LeftEye | BabbleZone_RightEye));
	if(!sharedEyeModel && BabbleZones_next(zones, first) != BabbleZone_COUNT) {
		fprintf(stderr, "invalid zone combination\n");
		return false;
	}
	mtx_lock_checked(&this->mutex);
	BabbleRuntime_waitLocked(this, zones, false);
	bool result = false;
	OrtEnv *env = nullptr;
	CHECK_ORT(goto unlock, "OrtApi::CreateEnv()", this->ort, CreateEnv(ORT_LOGGING_LEVEL_WARNING, "Babble", &env));
	OrtAllocator *allocator = nullptr;
	CHECK_ORT(goto unlock, "OrtApi::GetAllocatorWithDefaultOptions()", this->ort, GetAllocatorWithDefaultOptions(&allocator));
	OrtSession *session = nullptr;
	CHECK_ORT(goto unlock, "OrtApi::CreateSessionFromArray()", this->ort, CreateSessionFromArray(env, model, model_len, this->options, &session));
	size_t inputCount = 0, outputCount = 0;
	CHECK_ORT(goto fail0, "OrtApi::SessionGetInputCount()", this->ort, SessionGetInputCount(session, &inputCount));
	CHECK_ORT(goto fail0, "OrtApi::SessionGetOutputCount()", this->ort, SessionGetOutputCount(session, &outputCount));
	if(inputCount != 1) {
		fprintf(stderr, "wrong input count\n");
		goto fail0;
	}
	if(outputCount != 1) {
		fprintf(stderr, "wrong output count\n");
		goto fail0;
	}
	char *inputName = nullptr, *outputName = nullptr;
	CHECK_ORT(goto fail0, "OrtApi::SessionGetInputCount()", this->ort, SessionGetInputName(session, 0, allocator, &inputName));
	CHECK_ORT(goto fail1, "OrtApi::SessionGetOutputCount()", this->ort, SessionGetOutputName(session, 0, allocator, &outputName));
	int64_t shapes[2][4] = {};
	{
		OrtTypeInfo *typeInfo[2] = {};
		CHECK_ORT(goto fail2, "OrtApi::SessionGetInputTypeInfo()", this->ort, SessionGetInputTypeInfo(session, 0, &typeInfo[0]));
		CHECK_ORT({this->ort->ReleaseTypeInfo(drop_ptr(&typeInfo[0])); goto fail2;}, "OrtApi::SessionGetOutputTypeInfo()", this->ort, SessionGetOutputTypeInfo(session, 0, &typeInfo[1]));
		bool failed = false;
		static_assert(lengthof(typeInfo) == lengthof(shapes));
		for(unsigned i = 0; i < lengthof(typeInfo); this->ort->ReleaseTypeInfo(drop_ptr(&typeInfo[i])), ++i) {
			if(failed)
				continue;
			const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
			CHECK_ORT({failed = true; continue;}, "OrtApi::CastTypeInfoToTensorInfo()", this->ort, CastTypeInfoToTensorInfo(typeInfo[i], &tensorInfo));
			if(tensorInfo == nullptr) {
				fprintf(stderr, "not a tensor type\n");
				failed = true;
				continue;
			}
			enum ONNXTensorElementDataType elementType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
			CHECK_ORT({failed = true; continue;}, "OrtApi::GetTensorElementType()", this->ort, GetTensorElementType(tensorInfo, &elementType));
			size_t dimensionsCount = 0;
			CHECK_ORT({failed = true; continue;}, "OrtApi::GetDimensionsCount()", this->ort, GetDimensionsCount(tensorInfo, &dimensionsCount));
			if(elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
				fprintf(stderr, "wrong element type\n");
				failed = true;
				continue;
			}
			if(dimensionsCount != lengthof(*shapes)) {
				fprintf(stderr, "wrong dimension count\n");
				failed = true;
				continue;
			}
			CHECK_ORT({failed = true; continue;}, "OrtApi::GetDimensions()", this->ort, GetDimensions(tensorInfo, shapes[i], lengthof(*shapes)));
		}
		if(failed)
			goto fail2;
	}
	if(shapes[0][0] != 1 || shapes[0][1] != 1 || shapes[0][2] < 8 || shapes[0][2] > 4096 || shapes[0][3] < 8 || shapes[0][3] > 4096) {
		fprintf(stderr, "input shape out of range [%"PRIi64", %"PRIi64", %"PRIi64", %"PRIi64"]\n", shapes[0][0], shapes[0][1], shapes[0][2], shapes[0][3]);
		goto fail2;
	}
	if(shapes[1][0] != 1 || shapes[1][1] != 1 || shapes[1][2] != 1 || shapes[1][3] != ((first == BabbleZone_Mouth) ? 45 : 3)) {
		fprintf(stderr, "wrong output shape [%"PRIi64", %"PRIi64", %"PRIi64", %"PRIi64"]\n", shapes[1][0], shapes[1][1], shapes[1][2], shapes[1][3]);
		goto fail2;
	}
	struct {
		OrtValue *input, *output[2];
	} tensors[lengthof(this->contexts)] = {};
	for(enum BabbleZone i = first; i < lengthof(this->contexts); i = BabbleZones_next(zones, i)) {
		// TODO: allocate input in device-local memory?
		CHECK_ORT(goto fail3, "OrtApi::CreateTensorAsOrtValue()", this->ort, CreateTensorAsOrtValue(allocator, shapes[0], lengthof(shapes[0]), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tensors[i].input));
		CHECK_ORT(goto fail3, "OrtApi::CreateTensorAsOrtValue()", this->ort, CreateTensorAsOrtValue(allocator, shapes[1], lengthof(shapes[1]), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tensors[i].output[0]));
		CHECK_ORT(goto fail3, "OrtApi::CreateTensorAsOrtValue()", this->ort, CreateTensorAsOrtValue(allocator, shapes[1], lengthof(shapes[1]), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &tensors[i].output[1]));
	}
	result = true;
	for(enum BabbleZone i = first; i < lengthof(this->contexts); i = BabbleZones_next(zones, i)) {
		BabbleRuntime_cleanupContext(this, i);
		this->contexts[i].session = session;
		this->contexts[i].inputSize[0] = shapes[0][2];
		this->contexts[i].inputSize[1] = shapes[0][3];
		this->contexts[i].input = tensors[i].input;
		this->contexts[i].output[0] = tensors[i].output[0];
		this->contexts[i].output[1] = tensors[i].output[1];
		this->contexts[i].inputName = inputName;
		this->contexts[i].outputName = outputName;
	}
	if(false) {
		fail3: for(enum BabbleZone i = first; i < lengthof(this->contexts); i = BabbleZones_next(zones, i)) {
			if(tensors[i].input != nullptr)
				this->ort->ReleaseValue(drop_ptr(&tensors[i].input));
			if(tensors[i].output[0] != nullptr)
				this->ort->ReleaseValue(drop_ptr(&tensors[i].output[0]));
			if(tensors[i].output[1] != nullptr)
				this->ort->ReleaseValue(drop_ptr(&tensors[i].output[1]));
		}
		fail2: CHECK_ORT(, "OrtApi::AllocatorFree()", this->ort, AllocatorFree(allocator, drop_ptr(&outputName)));
		fail1: CHECK_ORT(, "OrtApi::AllocatorFree()", this->ort, AllocatorFree(allocator, drop_ptr(&inputName)));
		fail0: this->ort->ReleaseSession(drop_ptr(&session));
	}
	unlock: mtx_unlock_checked(&this->mutex);
	return result;
}

static void BabbleRuntime_onProcess(struct BabbleRuntime *const this, OrtValue *outputs[const], const size_t outputs_len, const OrtStatusPtr status, const enum BabbleZone zone) {
	mtx_lock_checked(&this->mutex);
	struct ZoneContext *const context = &this->contexts[zone];
	const BabbleZones group = context->group;
	this->pending &= ~(1u << zone);
	const bool done = ((this->pending & group) == 0), locked = ((this->locked & (1u << zone)) != 0), prevSwap = context->swap;
	context->swap = !context->swap;
	if(this->onData != nullptr)
		this->onData(this->onData_userptr, this, group, context->timestamp[context->swap]);
	if(locked) {
		context->swap = prevSwap;
		this->pendingSwap |= (1u << zone);
	}
	if(cnd_broadcast(&this->processFinished) != thrd_success) {
		fprintf(stderr, "cnd_broadcast() failed\n");
		abort();
	}
	mtx_unlock_checked(&this->mutex);
}

static void BabbleRuntime_onProcess_leftEye(void *const userptr, OrtValue *outputs[const], const size_t outputs_len, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, outputs_len, status, BabbleZone_LeftEye);
}

static void BabbleRuntime_onProcess_rightEye(void *const userptr, OrtValue *outputs[const], const size_t outputs_len, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, outputs_len, status, BabbleZone_RightEye);
}

static void BabbleRuntime_onProcess_mouth(void *const userptr, OrtValue *outputs[const], const size_t outputs_len, const OrtStatusPtr status) {
	BabbleRuntime_onProcess((struct BabbleRuntime*)userptr, outputs, outputs_len, status, BabbleZone_Mouth);
}

BabbleZones BabbleRuntime_pushFrame(struct BabbleRuntime *const this, const struct BabbleImage images[const], const uint32_t images_len, const BabbleTimestamp timestamp) {
	static const char zoneNames[][9] = {
		"LeftEye",
		"RightEye",
		"Mouth",
	};
	static_assert(lengthof(zoneNames) == lengthof(this->contexts));
	static void (*const onProcess[])(void*, OrtValue*[], size_t, OrtStatusPtr) = {
		BabbleRuntime_onProcess_leftEye,
		BabbleRuntime_onProcess_rightEye,
		BabbleRuntime_onProcess_mouth,
	};
	static_assert(lengthof(onProcess) == lengthof(this->contexts));

	if(this == nullptr)
		return false;
	mtx_lock_checked(&this->mutex);
	BabbleZones result = 0, group = 0;
	for(const struct BabbleImage *image = images; image < &images[images_len]; ++image) {
		if(image->zone >= lengthof(this->contexts)) {
			fprintf(stderr, "Invalid zone '%u'\n", (unsigned)image->zone);
			goto unlock;
		}
		if((group & (1u << image->zone)) != 0) {
			fprintf(stderr, "Cannot push multiple images for zone '%s'\n", zoneNames[image->zone]);
			goto unlock;
		}
		group |= (1u << image->zone);
	}
	BabbleRuntime_waitLocked(this, group, true);
	for(const struct BabbleImage *image = images; image < &images[images_len]; ++image) {
		struct ZoneContext *const context = &this->contexts[image->zone];
		void *inputData = nullptr;
		CHECK_ORT(goto unlock, "OrtApi::GetTensorMutableData()", this->ort, GetTensorMutableData(context->input, &inputData));
		if(!BabbleImage_resampleTo(image, (float*)inputData, context->inputSize[0] * context->inputSize[1], context->inputSize[0], context->inputSize[1])) {
			fprintf(stderr, "BabbleImage_resampleTo() failed\n");
			goto unlock;
		}
	}
	for(const struct BabbleImage *image = images; image < &images[images_len]; ++image) {
		struct ZoneContext *const context = &this->contexts[image->zone];
		context->timestamp[!context->swap] = timestamp;
		CHECK_ORT(group &= ~(1u << image->zone), "OrtApi::RunAsync()", this->ort, RunAsync(context->session, nullptr, (const char *const*)&context->inputName, (const OrtValue *const*)&context->input, 1,
			(const char *const*)&context->outputName, 1, &context->output[!context->swap], onProcess[image->zone], this));
	}
	for(enum BabbleZone i = BabbleZones_first(group); i < lengthof(this->contexts); i = BabbleZones_next(group, i))
		this->contexts[i].group = group;
	result = group;
	unlock: mtx_unlock_checked(&this->mutex);
	return result;
}

void BabbleRuntime_onData(struct BabbleRuntime *const this, BabbleRuntime_OnData *const onData, void *const onData_userptr) {
	if(this == nullptr)
		return;
	mtx_lock_checked(&this->mutex);
	this->onData = onData;
	this->onData_userptr = onData_userptr;
	mtx_unlock_checked(&this->mutex);
}

void BabbleRuntime_lockZones(struct BabbleRuntime *const this, const BabbleZones zones, const bool wait) {
	if(this == nullptr)
		return;
	mtx_lock_checked(&this->mutex);
	if(wait)
		BabbleRuntime_waitLocked(this, zones, false);
	const BabbleZones swap = (this->locked & ~zones) & this->pendingSwap;
	this->locked = zones;
	for(enum BabbleZone i = BabbleZones_first(swap); i < lengthof(this->contexts); i = BabbleZones_next(swap, i))
		this->contexts[i].swap = !this->contexts[i].swap;
	this->pendingSwap &= ~swap;
	if(swap != 0 && cnd_broadcast(&this->swapFinished) != thrd_success) {
		fprintf(stderr, "cnd_broadcast() failed\n");
		abort();
	}
	mtx_unlock_checked(&this->mutex);
}

/*BabbleTimestamp BabbleRuntime_getParams(struct BabbleRuntime *const this, const enum BabbleParam first, float params_out[const], const uint32_t params_len) {
	if(this == nullptr)
		return 0;
	mtx_lock_checked(&this->mutex);
	mtx_unlock_checked(&this->mutex);
}*/

/*BabbleTimestamp BabbleRuntime_getGazes(struct BabbleRuntime *const this, float (*const gazes_out)[2][4]) {
	if(this == nullptr)
		return 0;
	mtx_lock_checked(&this->mutex);
	mtx_unlock_checked(&this->mutex);
}*/
