#include "frames_native.h"
#include <cstdio>
#include <cstring>

int frames_probe_container_type(const char *path) {
    if (!path) return -1;
    FILE *f = std::fopen(path, "rb");
    if (!f) return -1;
    unsigned char header[16];
    size_t n = std::fread(header, 1, 16, f);
    std::fclose(f);
    if (n < 4) return -1;
    // EBML Header: 0x1A 0x45 0xDF 0xA3
    if (header[0] == 0x1A && header[1] == 0x45 && header[2] == 0xDF && header[3] == 0xA3) {
        return 0; // matroska
    }
    // ISO BMFF: bytes 4..<8 are 'ftyp', 'moov', etc.
    if (n >= 8) {
        char fourcc[5] = { (char)header[4], (char)header[5], (char)header[6], (char)header[7], 0 };
        if (std::strcmp(fourcc, "ftyp") == 0 || std::strcmp(fourcc, "moov") == 0 ||
            std::strcmp(fourcc, "wide") == 0 || std::strcmp(fourcc, "mdat") == 0 ||
            std::strcmp(fourcc, "free") == 0 || std::strcmp(fourcc, "skip") == 0 ||
            std::strcmp(fourcc, "styp") == 0) {
            return 1; // mp4
        }
    }
    // Fallback to extension
    const char *dot = std::strrchr(path, '.');
    if (dot) {
        if (strcasecmp(dot, ".mkv") == 0 || strcasecmp(dot, ".webm") == 0 ||
            strcasecmp(dot, ".mka") == 0) return 0;
        if (strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".mov") == 0 ||
            strcasecmp(dot, ".m4v") == 0) return 1;
    }
    return -1;
}

void frames_fast_copy(void *dst, const void *src, size_t n) {
    if (!dst || !src || n == 0) return;
    std::memcpy(dst, src, n);
}

// MARK: - Frame Queue Implementation
#include <os/lock.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <cmath>

struct FramesFrameSlot {
    void *pixelBuffer; // retained by queue until slot reuse
    double pts;
    double duration;
    int valid;
};

struct FramesFrameQueue {
    int maxCount;
    int ringMask;
    FramesFrameSlot *slots;
    std::atomic<uint64_t> producerHead{0};
    std::atomic<uint64_t> consumerTail{0};
    os_unfair_lock lock;
    double nominalDuration{1.0 / 30.0};
    double minimumAcceptedPTS{-INFINITY};
    FramesHeadroomCallback headroomCb{nullptr};
    void *headroomCtx{nullptr};
};

FramesFrameQueue *frames_frame_queue_create(int maxCount) {
    if (maxCount < 1) maxCount = 1;
    // round up to power of two for masking, min 16
    int ring = 16;
    while (ring < maxCount && ring < 256) ring <<= 1;
    if (ring < 16) ring = 16;
    FramesFrameQueue *q = new FramesFrameQueue();
    q->maxCount = maxCount;
    q->ringMask = ring - 1;
    q->slots = new FramesFrameSlot[ring]();
    for (int i = 0; i < ring; i++) {
        q->slots[i].pixelBuffer = nullptr;
        q->slots[i].valid = 0;
    }
    q->lock = OS_UNFAIR_LOCK_INIT;
    return q;
}

void frames_frame_queue_destroy(FramesFrameQueue *q) {
    if (!q) return;
    frames_frame_queue_remove_all(q);
    delete[] q->slots;
    delete q;
}

int frames_frame_queue_count(FramesFrameQueue *q) {
    if (!q) return 0;
    os_unfair_lock_lock(&q->lock);
    uint64_t c = q->producerHead.load() - q->consumerTail.load();
    os_unfair_lock_unlock(&q->lock);
    if (c > (uint64_t)q->maxCount) c = 0;
    return (int)c;
}

int frames_frame_queue_max_count(FramesFrameQueue *q) {
    return q ? q->maxCount : 0;
}

void frames_frame_queue_set_nominal(FramesFrameQueue *q, double d) {
    if (!q) return;
    os_unfair_lock_lock(&q->lock);
    q->nominalDuration = (d > 0.001) ? d : (1.0 / 30.0);
    os_unfair_lock_unlock(&q->lock);
}

void frames_frame_queue_set_headroom_callback(FramesFrameQueue *q, FramesHeadroomCallback cb, void *ctx) {
    if (!q) return;
    os_unfair_lock_lock(&q->lock);
    q->headroomCb = cb;
    q->headroomCtx = ctx;
    os_unfair_lock_unlock(&q->lock);
}

void frames_frame_queue_remove_all(FramesFrameQueue *q) {
    if (!q) return;
    os_unfair_lock_lock(&q->lock);
    int ring = q->ringMask + 1;
    uint64_t head = q->producerHead.load();
    uint64_t tail = q->consumerTail.load();
    uint64_t curr = tail;
    while (curr < head) {
        int slot = (int)(curr & (uint64_t)q->ringMask);
        if (q->slots[slot].valid && q->slots[slot].pixelBuffer) {
            CFRelease(q->slots[slot].pixelBuffer);
            q->slots[slot].pixelBuffer = nullptr;
            q->slots[slot].valid = 0;
        }
        curr++;
    }
    q->producerHead.store(head);
    q->consumerTail.store(head);
    q->minimumAcceptedPTS = -INFINITY;
    FramesHeadroomCallback cb = q->headroomCb;
    void *ctx = q->headroomCtx;
    os_unfair_lock_unlock(&q->lock);
    if (cb) cb(ctx);
}

void frames_frame_queue_reset_for_seek(FramesFrameQueue *q, double target) {
    if (!q) return;
    frames_frame_queue_remove_all(q);
    os_unfair_lock_lock(&q->lock);
    q->minimumAcceptedPTS = (target > 0.0) ? target - 0.25 : -INFINITY;
    os_unfair_lock_unlock(&q->lock);
}

int frames_frame_queue_enqueue(FramesFrameQueue *q, void *pixelBuffer, double pts, double duration) {
    if (!q || !pixelBuffer) return -1;
    os_unfair_lock_lock(&q->lock);
    uint64_t head = q->producerHead.load();
    uint64_t tail = q->consumerTail.load();
    // Backpressure: if queue full, drop
    if (head - tail >= (uint64_t)q->maxCount) {
        os_unfair_lock_unlock(&q->lock);
        CFRelease(pixelBuffer);
        return -1;
    }
    // minimumAcceptedPTS filter
    if (q->minimumAcceptedPTS != -INFINITY && pts < q->minimumAcceptedPTS) {
        os_unfair_lock_unlock(&q->lock);
        CFRelease(pixelBuffer);
        return -1;
    }
    int slot = (int)(head & (uint64_t)q->ringMask);
    // Release old ref if slot reused
    if (q->slots[slot].valid && q->slots[slot].pixelBuffer) {
        CFRelease(q->slots[slot].pixelBuffer);
    }
    CFRetain(pixelBuffer);
    q->slots[slot].pixelBuffer = pixelBuffer;
    q->slots[slot].pts = pts;
    q->slots[slot].duration = duration;
    q->slots[slot].valid = 1;
    q->producerHead.store(head + 1);
    os_unfair_lock_unlock(&q->lock);
    return 0;
}

int frames_frame_queue_dequeue(FramesFrameQueue *q, double masterClock, double frameDuration, void **outBuffer, double *outPts) {
    if (!q || !outBuffer || !outPts) return 0;
    *outBuffer = nullptr;
    *outPts = 0;
    int delivered = 0;
    os_unfair_lock_lock(&q->lock);
    uint64_t tail = q->consumerTail.load();
    uint64_t head = q->producerHead.load();

    // Prune stale frames
    int dropped = 0;
    while ((head > tail) && ((head - tail) > 1)) {
        int slot = (int)(tail & (uint64_t)q->ringMask);
        if (!q->slots[slot].valid || !q->slots[slot].pixelBuffer) break;
        double candPts = q->slots[slot].pts;
        double dur = (frameDuration > 0.001) ? frameDuration : q->nominalDuration;
        double lagThreshold = (dur * 2.0 > 0.033) ? (dur * 2.0) : 0.033;
        if (candPts < masterClock - lagThreshold) {
            CFRelease(q->slots[slot].pixelBuffer);
            q->slots[slot].pixelBuffer = nullptr;
            q->slots[slot].valid = 0;
            tail++;
            dropped = 1;
        } else {
            break;
        }
    }
    if (dropped) {
        q->consumerTail.store(tail);
        head = q->producerHead.load();
    }

    if (head > tail) {
        int slot = (int)(tail & (uint64_t)q->ringMask);
        if (q->slots[slot].valid && q->slots[slot].pixelBuffer) {
            double candPts = q->slots[slot].pts;
            double dur = (frameDuration > 0.001) ? frameDuration : q->nominalDuration;
            double deliveryTolerance = dur * 0.75;
            if (deliveryTolerance < 0.005) deliveryTolerance = 0.005;
            if (deliveryTolerance > 0.020) deliveryTolerance = 0.020;
            if (candPts <= masterClock + deliveryTolerance) {
                *outBuffer = q->slots[slot].pixelBuffer; // borrowed
                *outPts = candPts;
                q->slots[slot].valid = 0;
                q->consumerTail.store(tail + 1);
                delivered = 1;
                FramesHeadroomCallback cb = q->headroomCb;
                void *ctx = q->headroomCtx;
                os_unfair_lock_unlock(&q->lock);
                if (cb) cb(ctx);
                return delivered;
            }
        }
    }
    os_unfair_lock_unlock(&q->lock);
    return delivered;
}
