#ifndef FRAMES_NATIVE_H
#define FRAMES_NATIVE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 = matroska, 1 = mp4, -1 = unknown
int frames_probe_container_type(const char *path);

// Zero-copy-ish fast memcpy for hot paths (uses restrict + prefetch hints)
void frames_fast_copy(void *dst, const void *src, size_t n);


// MARK: - Lock-free SPSC Frame Queue (C++ core)
typedef struct FramesFrameQueue FramesFrameQueue;
typedef void (*FramesHeadroomCallback)(void *ctx);

FramesFrameQueue *frames_frame_queue_create(int maxCount);
void frames_frame_queue_destroy(FramesFrameQueue *q);
int frames_frame_queue_count(FramesFrameQueue *q);
int frames_frame_queue_max_count(FramesFrameQueue *q);
void frames_frame_queue_set_nominal(FramesFrameQueue *q, double d);
void frames_frame_queue_set_headroom_callback(FramesFrameQueue *q, FramesHeadroomCallback cb, void *ctx);
void frames_frame_queue_remove_all(FramesFrameQueue *q);
void frames_frame_queue_reset_for_seek(FramesFrameQueue *q, double target);
int frames_frame_queue_enqueue(FramesFrameQueue *q, void *pixelBuffer, double pts, double duration);
int frames_frame_queue_dequeue(FramesFrameQueue *q, double masterClock, double frameDuration, void **outBuffer, double *outPts);
#ifdef __cplusplus
}
#endif
#endif
