// Copyright 2025, rcelyte
// SPDX-License-Identifier: Apache-2.0

#define VideoContext pw_core
#include "video.h"
#undef VideoContext
#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/latency-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <stdatomic.h>
#include <threads.h>

[[maybe_unused]] static inline void *drop_ptr_(void *const ptr) {void *const inner = *(void**)ptr; *(void**)ptr = nullptr; return inner;}
#define drop_ptr(_ptr) ((typeof(*(_ptr))){drop_ptr_(_ptr)})
#define lengthof(_array) (sizeof(_array) / sizeof((_array)[0]))

// TODO: migrate to VideoContext->pw_filter, VideoSource->pw_port

struct VideoSource {
	_Atomic(uint64_t) refCount;
	struct pw_stream *stream;
	struct spa_hook hook;
	uint64_t latencyOffset;
	struct VideoSource_Mode mode;
	void (*onMode)(void *userptr, struct VideoSource_Mode mode);
	void *onMode_userptr;
	void (*onFrame)(void *userptr, const uint8_t data[], uint32_t data_len, uint64_t timestamp);
	void *onFrame_userptr;
};

struct VideoContext {
	_Atomic(uint64_t) refCount;
	mtx_t mutex;
	char name[];
};

struct VideoContext_Lock {
	struct VideoContext *data;
	struct pw_loop *loop;
};

[[gnu::constructor]] static void init_() {
	pw_init(nullptr, nullptr);
}

static inline struct VideoContext_Lock VideoContext_lock(struct pw_core *const core, struct pw_loop *const loop) {
	const struct VideoContext_Lock lock = {
		.data = pw_core_get_user_data(core),
		.loop = loop,
	};
	if(mtx_lock(&lock.data->mutex) != thrd_success) {
		fprintf(stderr, "mtx_lock() failed\n");
		abort();
	}
	if(loop != nullptr)
		pw_loop_enter(loop);
	return lock;
}

static inline void VideoContext_unlock(struct VideoContext_Lock lock) {
	if(lock.loop != nullptr)
		pw_loop_leave(lock.loop);
	if(mtx_unlock(&lock.data->mutex) != thrd_success) {
		fprintf(stderr, "mtx_lock() failed\n");
		abort();
	}
}

[[gnu::visibility("default")]] struct VideoSource_Mode VideoSource_currentMode(struct VideoSource *const this) {
	if(this == nullptr)
		return (struct VideoSource_Mode){};
	const struct VideoContext_Lock lock = VideoContext_lock(pw_stream_get_core(this->stream), nullptr);
	const struct VideoSource_Mode mode = this->mode;
	VideoContext_unlock(lock);
	return mode;
}

[[gnu::visibility("default")]] void VideoSource_onMode(struct VideoSource *const this, void (*const callback)(void *userptr, struct VideoSource_Mode mode), void *const userptr) {
	if(this == nullptr)
		return;
	const struct VideoContext_Lock lock = VideoContext_lock(pw_stream_get_core(this->stream), nullptr);
	this->onMode = callback;
	this->onMode_userptr = userptr;
	VideoContext_unlock(lock);
}

[[gnu::visibility("default")]] void VideoSource_onFrame(struct VideoSource *const this, void (*const callback)(void *userptr, const uint8_t data[], uint32_t data_len, uint64_t timestamp), void *const userptr) {
	if(this == nullptr)
		return;
	const struct VideoContext_Lock lock = VideoContext_lock(pw_stream_get_core(this->stream), nullptr);
	this->onFrame = callback;
	this->onFrame_userptr = userptr;
	VideoContext_unlock(lock);
}

[[gnu::visibility("default")]] void VideoSource_play(struct VideoSource *const this, const bool play) {
	if(this == nullptr)
		return;
	const struct VideoContext_Lock lock = VideoContext_lock(pw_stream_get_core(this->stream), pw_stream_get_data_loop(this->stream));
	pw_stream_set_active(this->stream, play);
	VideoContext_unlock(lock);
}

static void VideoSource_onProcess(void *const userptr) {
	struct VideoSource *const this = (struct VideoSource*)userptr;
	struct pw_buffer *const buffer = pw_stream_dequeue_buffer(this->stream);
	if(buffer == nullptr) {
		pw_log_warn("out of buffers: %m");
		return;
	}
	const struct spa_data bufferData = buffer->buffer->datas[0];
	const uint32_t size = bufferData.chunk->size;
	if(this->onFrame != nullptr && bufferData.data != nullptr)
		this->onFrame(this->onFrame_userptr, bufferData.data, size, buffer->time - this->latencyOffset);
	pw_stream_queue_buffer(this->stream, buffer);
}

static void VideoSource_onParamChanged(void *const userptr, const uint32_t id, const struct spa_pod *const param) {
	struct VideoSource *const this = (struct VideoSource*)userptr;
	switch(id) {
		case SPA_PARAM_Format: {
			struct spa_video_info format = {};
			if(param == nullptr || spa_format_video_parse(param, &format) < 0)
				format = (struct spa_video_info){};
			switch(format.media_subtype) {
				case SPA_MEDIA_SUBTYPE_raw: {
					this->mode = (struct VideoSource_Mode){
						.size = {format.info.raw.size.width, format.info.raw.size.height},
						.framerate = {format.info.raw.framerate.num, format.info.raw.framerate.denom},
					};
					switch(format.info.raw.format) {
						case SPA_VIDEO_FORMAT_YUY2: this->mode.format = VideoFormat_YUY2; break;
						case SPA_VIDEO_FORMAT_BGRx: this->mode.format = VideoFormat_BGRx; break;
						case SPA_VIDEO_FORMAT_NV12: this->mode.format = VideoFormat_NV12; break;
						case SPA_VIDEO_FORMAT_GRAY8: this->mode.format = VideoFormat_Gray8; break;
						case SPA_VIDEO_FORMAT_GRAY16_BE: this->mode.format = VideoFormat_Gray16BE; break;
						case SPA_VIDEO_FORMAT_GRAY16_LE: this->mode.format = VideoFormat_Gray16LE; break;
						default: fprintf(stderr, "Unexpected stream format: %s\n", spa_debug_type_find_name(spa_type_video_format, format.info.raw.format));
					}
				} break;
				case SPA_MEDIA_SUBTYPE_dsp: break; // TODO
				case SPA_MEDIA_SUBTYPE_h264: this->mode = (struct VideoSource_Mode){
					.format = VideoFormat_MJPEG,
					.size = {format.info.h264.size.width, format.info.h264.size.height},
					.framerate = {format.info.h264.framerate.num, format.info.h264.framerate.denom},
				}; break;
				case SPA_MEDIA_SUBTYPE_mjpg: this->mode = (struct VideoSource_Mode){
					.format = VideoFormat_H264,
					.size = {format.info.mjpg.size.width, format.info.mjpg.size.height},
					.framerate = {format.info.mjpg.framerate.num, format.info.mjpg.framerate.denom},
				}; break;
			}
			fprintf(stderr, "format [format=%"PRIu32" size=%"PRIu32"x%"PRIu32" framerate=%"PRIu32"/%"PRIu32"]\n",
				this->mode.format, this->mode.size.width, this->mode.size.height, this->mode.framerate.num, this->mode.framerate.denom);
			if(this->onMode != nullptr)
				this->onMode(this->onMode_userptr, this->mode);
		} break;
		case SPA_PARAM_Latency: {
			struct spa_latency_info latency = {};
			if(spa_latency_parse(param, &latency) < 0)
				latency = (struct spa_latency_info){};
			this->latencyOffset = latency.min_ns;
			fprintf(stderr, "latency [direction=%s min_quantum=%f max_quantum=%f min_rate=%"PRIu32" max_rate=%"PRIu32" min_ns=%"PRIu64" max_ns=%"PRIu64"]\n",
				latency.direction ? "OUTPUT" : "INPUT", latency.min_quantum, latency.max_quantum, latency.min_rate, latency.max_rate, latency.min_ns, latency.max_ns);
		} break;
	}
	if(id != SPA_PARAM_Format)
		return;
}

// TODO: return existing stream if already opened
static struct VideoSource *VideoSource_Open(struct pw_core *const core, const uint32_t targetId, const char path[const], const uint32_t path_len) {
	struct VideoSource *this = calloc(1, sizeof(*this));
	if(this == nullptr)
		return nullptr;
	atomic_init(&this->refCount, 1);
	const struct VideoContext_Lock lock = VideoContext_lock(core, pw_context_get_main_loop(pw_core_get_context(core)));
	struct pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Camera", nullptr);
	if(props == nullptr)
		goto fail0;
	if(path_len != 0)
		pw_properties_setf(props, PW_KEY_TARGET_OBJECT, "%.*s", (unsigned)path_len, path);
	this->stream = pw_stream_new(core, ((struct VideoContext*)pw_core_get_user_data(core))->name, drop_ptr(&props));
	if(this->stream == nullptr)
		goto fail0;
	static const struct pw_stream_events events = {
		PW_VERSION_STREAM_EVENTS,
		.param_changed = VideoSource_onParamChanged,
		.process = VideoSource_onProcess,
	};
	pw_stream_add_listener(this->stream, &this->hook, &events, this);
	{
		uint8_t buffer[1024];
		struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
		const struct spa_pod *params[1] = { // TODO: more formats
			spa_pod_builder_add_object(&builder,
				SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
				SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
				SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), //SPA_POD_CHOICE_ENUM_Id(2, SPA_MEDIA_SUBTYPE_raw, SPA_MEDIA_SUBTYPE_bayer), // SPA_MEDIA_SUBTYPE_mjpg
				SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(2, SPA_VIDEO_FORMAT_GRAY8, SPA_VIDEO_FORMAT_YUY2),
				SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&SPA_RECTANGLE(64, 64), &SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(4096, 4096)),
				SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(25, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(1000, 1))),
		};
		pw_stream_connect(this->stream, PW_DIRECTION_INPUT, targetId, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE | PW_STREAM_FLAG_MAP_BUFFERS, params, lengthof(params)); // TODO: error check?
	}
	VideoContext_unlock(lock);
	VideoContext_ref(core);
	return drop_ptr(&this);
	fail0: free(drop_ptr(&this));
	VideoContext_unlock(lock);
	return nullptr;
}

[[gnu::visibility("default")]] void VideoSource_ref(struct VideoSource *const this) {
	if(this != nullptr)
		atomic_fetch_add(&this->refCount, 1);
}

[[gnu::visibility("default")]] bool VideoSource_unref(struct VideoSource *this, void **const onMode_out, void **const onFrame_out) {
	uint32_t refCount;
	if(this == nullptr || (refCount = atomic_fetch_sub(&this->refCount, 1)) >= 2)
		return false;
	if(refCount != 1) {
		fprintf(stderr, "Unmatched VideoSource_unref()\n");
		abort();
	}
	struct pw_core *const core = pw_stream_get_core(this->stream);
	const struct VideoContext_Lock lock = VideoContext_lock(core, pw_stream_get_data_loop(this->stream));
	pw_stream_destroy(drop_ptr(&this->stream));
	if(onMode_out != nullptr)
		*onMode_out = this->onMode_userptr;
	if(onFrame_out != nullptr)
		*onFrame_out = this->onFrame_userptr;
	free(drop_ptr(&this));
	VideoContext_unlock(lock);
	VideoContext_unref(core);
	return true;
}

[[gnu::visibility("default")]] void **VideoSource_onMode_modifyUserptrLocked(struct VideoSource *const this) {
	if(this == nullptr)
		return nullptr;
	VideoContext_lock(pw_stream_get_core(this->stream), nullptr);
	return &this->onMode_userptr;
}

[[gnu::visibility("default")]] void **VideoSource_onFrame_modifyUserptrLocked(struct VideoSource *const this) {
	if(this == nullptr)
		return nullptr;
	VideoContext_lock(pw_stream_get_core(this->stream), nullptr);
	return &this->onFrame_userptr;
}

[[gnu::visibility("default")]] void VideoSource_unlock(struct VideoSource *this) {
	if(this != nullptr)
		VideoContext_unlock((struct VideoContext_Lock){.data = pw_core_get_user_data(pw_stream_get_core(this->stream))});
}

[[gnu::visibility("default")]] struct pw_core *VideoContext_new(const char name[const], const uint32_t name_len, intptr_t *const waitHandle_out) {
	if(waitHandle_out == nullptr)
		return nullptr;
	struct pw_loop *loop = pw_loop_new(nullptr);
	if(loop == nullptr)
		return nullptr;
	struct pw_context *context = pw_context_new(loop, nullptr, 0);
	if(context == nullptr)
		goto fail0;
	struct pw_core *core = pw_context_connect(context, nullptr, sizeof(struct VideoContext) + name_len + 1);
	if(core == nullptr)
		goto fail1;
	struct VideoContext *const this = (struct VideoContext*)pw_core_get_user_data(core);
	atomic_init(&this->refCount, 1);
	if(mtx_init(&this->mutex, mtx_plain | mtx_recursive) != thrd_success)
		goto fail2;
	memcpy(this->name, name, name_len);
	this->name[name_len] = 0;
	*waitHandle_out = pw_loop_get_fd(loop);
	return drop_ptr(&core);
	fail2: pw_core_disconnect(drop_ptr(&core));
	fail1: pw_context_destroy(drop_ptr(&context));
	fail0: pw_loop_destroy(drop_ptr(&loop));
	*waitHandle_out = -1;
	return nullptr;
}

[[gnu::visibility("default")]] void VideoContext_ref(struct pw_core *const core) {
	if(core != nullptr)
		atomic_fetch_add(&((struct VideoContext*)pw_core_get_user_data(core))->refCount, 1);
}

[[gnu::visibility("default")]] bool VideoContext_unref(struct pw_core *core) {
	struct VideoContext *const this = (struct VideoContext*)pw_core_get_user_data(core);
	uint32_t refCount;
	if(core == nullptr || (refCount = atomic_fetch_sub(&this->refCount, 1)) >= 2)
		return false;
	if(refCount != 1) {
		fprintf(stderr, "Unmatched VideoContext_unref()\n");
		abort();
	}
	// TODO: refcount (keep alive until all VideoSource instances are released)
	struct pw_context *context = pw_core_get_context(core);
	struct pw_loop *loop = pw_context_get_main_loop(context);
	mtx_destroy(&this->mutex);
	pw_core_disconnect(drop_ptr(&core));
	pw_context_destroy(drop_ptr(&context));
	pw_loop_destroy(drop_ptr(&loop));
	return true;
}

[[gnu::visibility("default")]] struct VideoSource *VideoContext_open(struct pw_core *const core, const enum VideoContext_SourceId id) {
	return (core != nullptr) ? VideoSource_Open(core, (id == VideoContext_SourceId_None) ? PW_ID_ANY : (uint32_t)id, nullptr, 0) : nullptr;
}

[[gnu::visibility("default")]] struct VideoSource *VideoContext_openPath(struct pw_core *const core, const char path[const], const uint32_t path_len) {
	return (core != nullptr) ? VideoSource_Open(core, PW_ID_ANY, path, path_len) : nullptr;
}

[[gnu::visibility("default")]] void VideoContext_tick(struct pw_core *const core) {
	if(core == nullptr)
		return;
	const struct VideoContext_Lock lock = VideoContext_lock(core, pw_context_get_main_loop(pw_core_get_context(core)));
	pw_loop_iterate(lock.loop, 0);
	VideoContext_unlock(lock);
}
