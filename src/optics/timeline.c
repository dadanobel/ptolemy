/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "timeline.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint64_t id, first, last;
    unsigned done, success;
} Batch;
typedef struct {
    uint64_t id, after;
} Fence;
struct PtR2Timeline {
    uint64_t epoch, next, issued, submitted, gpu_done, failure;
    uint32_t nb, nf, head, count;
    Batch *batch;
    Fence *fence;
};
/* Creates the completion timeline: batches of GPU work and the fences the guest waits on. */
PtR2Timeline *pt_r2_timeline_create(uint32_t b, uint32_t f) {
    if (!b || b > 1024 || !f || f > 4096)
        return NULL;
    PtR2Timeline *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->batch = calloc(b, sizeof(Batch));
    t->fence = calloc(f, sizeof(Fence));
    if (!t->batch || !t->fence) {
        pt_r2_timeline_destroy(t);
        return NULL;
    }
    t->nb = b;
    t->nf = f;
    t->epoch = 1;
    return t;
}
void pt_r2_timeline_destroy(PtR2Timeline *t) {
    if (t) {
        free(t->batch);
        free(t->fence);
        free(t);
    }
}
int pt_r2_timeline_reset(PtR2Timeline *t) {
    if (!t || t->epoch == UINT64_MAX)
        return 0;
    t->epoch++;
    t->issued = t->submitted = t->gpu_done = t->failure = 0;
    t->head = t->count = 0;
    memset(t->batch, 0, t->nb * sizeof(Batch));
    memset(t->fence, 0, t->nf * sizeof(Fence));
    return 1;
}
uint64_t pt_r2_timeline_issue(PtR2Timeline *t) {
    return !t || t->failure || t->issued == UINT64_MAX ? 0 : ++t->issued;
}
static uint64_t id(PtR2Timeline *t) {
    return t->next == UINT64_MAX ? 0 : ++t->next;
}
PtR2TimelineResult pt_r2_timeline_submit(PtR2Timeline *t, uint64_t last, PtR2TimelineToken *out) {
    if (!t || !out || last <= t->submitted || last > t->issued)
        return PT_R2_INVALID;
    if (t->failure)
        return PT_R2_FAILED;
    if (t->count == t->nb)
        return PT_R2_FULL;
    uint64_t key = id(t);
    if (!key)
        return PT_R2_FULL;
    t->batch[(t->head + t->count) % t->nb] = (Batch){.id = key, .first = t->submitted + 1, .last = last};
    t->count++;
    t->submitted = last;
    *out = (PtR2TimelineToken){t->epoch, key};
    return PT_R2_READY;
}
/* Marks a submitted batch complete (or failed) and advances the completed fence. */
PtR2TimelineResult pt_r2_timeline_complete(PtR2Timeline *t, PtR2TimelineToken token, int success) {
    if (!t || token.epoch != t->epoch)
        return PT_R2_STALE;
    Batch *b = NULL;
    for (unsigned i = 0; i < t->count; i++) {
        Batch *candidate = &t->batch[(t->head + i) % t->nb];
        if (candidate->id == token.id) {
            b = candidate;
            break;
        }
    }
    if (!b)
        return PT_R2_STALE;
    if (b->done)
        return PT_R2_INVALID;
    b->done = 1;
    b->success = !!success;
    if (!success && (!t->failure || b->first < t->failure))
        t->failure = b->first;
    while (t->count && t->batch[t->head].done) {
        Batch *first = &t->batch[t->head];
        if (first->success && (!t->failure || first->last < t->failure))
            t->gpu_done = first->last;
        memset(first, 0, sizeof *first);
        t->head = (t->head + 1) % t->nb;
        t->count--;
    }
    return success ? PT_R2_READY : PT_R2_FAILED;
}
static Fence *fence(PtR2Timeline *t, PtR2TimelineToken token) {
    if (!t || token.epoch != t->epoch || !token.id)
        return NULL;
    for (unsigned i = 0; i < t->nf; i++)
        if (t->fence[i].id == token.id)
            return &t->fence[i];
    return NULL;
}
PtR2TimelineResult pt_r2_fence_create(PtR2Timeline *t, uint64_t after, PtR2TimelineToken *out) {
    if (!t || !out || after > t->issued)
        return PT_R2_INVALID;
    Fence value = {.after = after};
    for (unsigned i = 0; i < t->nf; i++)
        if (!t->fence[i].id) {
            value.id = id(t);
            if (!value.id)
                return PT_R2_FULL;
            t->fence[i] = value;
            *out = (PtR2TimelineToken){t->epoch, value.id};
            return PT_R2_READY;
        }
    return PT_R2_FULL;
}
PtR2TimelineResult pt_r2_fence_poll(PtR2Timeline *t, PtR2TimelineToken token) {
    Fence *f = fence(t, token);
    if (!f)
        return PT_R2_STALE;
    if (t->failure && f->after >= t->failure)
        return PT_R2_FAILED;
    return t->gpu_done < f->after ? PT_R2_PENDING : PT_R2_READY;
}
PtR2TimelineResult pt_r2_fence_release(PtR2Timeline *t, PtR2TimelineToken token) {
    Fence *f = fence(t, token);
    if (!f)
        return PT_R2_STALE;
    memset(f, 0, sizeof *f);
    return PT_R2_READY;
}
uint64_t pt_r2_timeline_gpu_done(const PtR2Timeline *t) {
    return t ? t->gpu_done : 0;
}
