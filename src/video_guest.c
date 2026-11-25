/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pt_video.h: the sessions, each one's queue of frames, the limits and the stub decoder. */
#include "pt_video.h"
#include "pt_agent.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    FREE,
    PENDING,
    READY,
    FAILED,
    DROPPED /* dropped while the decoder has it: the slot is free again when the decoder is done */
};
typedef struct {
    uint32_t frame;
    int state;
    void *picture;
} Slot;
typedef struct {
    uint32_t id, width, height;
    const PtVideoBackend *backend;
    void *decoder;
    uint64_t used;
    pthread_mutex_t submitting; /* held around submit and close: a decoder is never closed under a submission */
    Slot slot[PTV_QUEUE];
} Session;
/* The lock guards the table and every slot. It is never held around submit or close: the decoder's completions
 * (pt_video_done) take it, and may come from inside submit. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Session *table[PTV_SESSIONS];
static const PtVideoBackend *backend;
static int chosen;
static uint32_t last_id;

/* Milliseconds of the monotonic clock: the default clock for idle sessions. */
static uint64_t monotonic(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static uint64_t (*now)(void) = monotonic;
void pt_video_clock(uint64_t (*milliseconds)(void)) {
    now = milliseconds ? milliseconds : monotonic;
}
void pt_video_backend(const PtVideoBackend *b) {
    pthread_mutex_lock(&lock);
    backend = b;
    chosen = 1;
    pthread_mutex_unlock(&lock);
}
static const PtVideoBackend *choose(void) { /* under the lock */
    if (!chosen) {
        const char *v = getenv("PT_VIDEO");
        chosen = 1;
        if (v && !strcmp(v, "stub"))
            backend = &pt_video_stub;
#ifdef __APPLE__
        else if (!v || strcmp(v, "0"))
            backend = pt_video_macos();
#endif
    }
    return backend;
}
/* Is there a decoder at all (PT_VIDEO not 0)? The guest component registers only if so. */
int pt_video_enabled(void) {
    pthread_mutex_lock(&lock);
    int on = choose() != NULL;
    pthread_mutex_unlock(&lock);
    return on;
}
/* Under the lock: sessions idle for too long leave the table into gone[], for end() after the lock; then the session
 * of this id, or NULL. */
static Session *find(uint32_t id, Session *gone[PTV_SESSIONS]) {
    Session *s = NULL;
    uint64_t t = now();
    for (unsigned i = 0; i < PTV_SESSIONS; i++) {
        if (table[i] && t - table[i]->used >= PTV_IDLE_MS) {
            gone[i] = table[i];
            table[i] = NULL;
        }
        if (table[i] && table[i]->id == id) {
            s = table[i];
            s->used = t;
        }
    }
    return s;
}
/* Sessions already out of the table: each waits for a submission in flight and for its decoder's last picture, then
 * frees the pictures nobody fetched. */
static void end(Session *gone[PTV_SESSIONS]) {
    for (unsigned i = 0; i < PTV_SESSIONS; i++) {
        Session *s = gone[i];
        if (!s)
            continue;
        pthread_mutex_lock(&s->submitting);
        s->backend->close(s->decoder);
        pthread_mutex_unlock(&s->submitting);
        for (unsigned k = 0; k < PTV_QUEUE; k++)
            if (s->slot[k].state == READY)
                s->backend->release(s->slot[k].picture);
        pthread_mutex_destroy(&s->submitting);
        free(s);
    }
}
/* Under the lock: the slot that holds this frame, or NULL. */
static Slot *slot_of(Session *s, uint32_t frame) {
    for (unsigned k = 0; k < PTV_QUEUE; k++)
        if (s->slot[k].state != FREE && s->slot[k].frame == frame)
            return &s->slot[k];
    return NULL;
}
static void vacate(Session *s, Slot *q) { /* a slot the decoder is done with */
    if (q->state == READY)
        s->backend->release(q->picture);
    q->state = FREE;
}
/* A new session for a stream: its decoder is made from the avcC; *session gets the id the guest names it by. */
int pt_video_open(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, uint32_t *session) {
    if (!avcc || !n || !session || !width || !height || width > PTV_MAX_WIDTH || height > PTV_MAX_HEIGHT || (width & 1))
        return PTA_INVALID; /* 2vuy pairs: an odd width goes to the guest's own decoder */
    Session *gone[PTV_SESSIONS] = {0}, *s = NULL;
    int rc = PTA_BUSY;
    pthread_mutex_lock(&lock);
    (void)find(0, gone);
    const PtVideoBackend *b = choose();
    for (unsigned i = 0; b && i < PTV_SESSIONS && rc == PTA_BUSY; i++)
        if (!table[i] && (s = calloc(1, sizeof *s))) {
            /* open makes no pictures, so it may run under the lock */
            if ((s->decoder = b->open(avcc, n, width, height, s))) {
                if (!last_id)
                    last_id = (uint32_t)time(NULL) << 4; /* an id of an earlier engine is unlikely to name a session */
                if (!++last_id)
                    ++last_id;
                *session = s->id = last_id;
                s->width = width;
                s->height = height;
                s->backend = b;
                s->used = now();
                pthread_mutex_init(&s->submitting, NULL);
                table[i] = s;
                rc = PTA_OK;
            } else {
                free(s);
                rc = PTA_DENIED;
            }
        }
    if (!b)
        rc = PTA_DENIED;
    pthread_mutex_unlock(&lock);
    end(gone);
    return rc;
}
/* Queues one compressed frame and hands it to the decoder. PTA_AGAIN while the queue is full or another thread
 * submits. */
int pt_video_decode(uint32_t session, uint32_t frame, const uint8_t *data, uint32_t n) {
    if (!data || !n || n > PTV_MAX_FRAME || frame == PTV_ALL)
        return PTA_INVALID;
    Session *gone[PTV_SESSIONS] = {0};
    Slot *q = NULL;
    int rc = PTA_CANCELLED;
    pthread_mutex_lock(&lock);
    Session *s = find(session, gone);
    if (s) {
        for (unsigned k = 0; k < PTV_QUEUE && !q; k++)
            if (s->slot[k].state == FREE)
                q = &s->slot[k];
        if (slot_of(s, frame))
            rc = PTA_INVALID;
        else if (!q || pthread_mutex_trylock(&s->submitting))
            rc = PTA_AGAIN; /* the queue is full, or another thread is submitting to this session */
        else {
            q->frame = frame;
            q->state = PENDING;
            q->picture = NULL;
            rc = PTA_OK;
        }
    }
    pthread_mutex_unlock(&lock);
    if (rc == PTA_OK) { /* s cannot end while its submitting lock is held */
        if (s->backend->submit(s->decoder, frame, data, n)) {
            pthread_mutex_lock(&lock);
            if ((q = slot_of(s, frame)))
                vacate(s, q);
            pthread_mutex_unlock(&lock);
            rc = PTA_DENIED;
        }
        pthread_mutex_unlock(&s->submitting);
    }
    end(gone);
    return rc;
}
/* The decoder's answer for one frame, from any thread: the picture waits in its slot for pt_video_fetch. */
void pt_video_done(void *owner, uint32_t frame, void *picture) {
    Session *s = owner; /* alive: its decoder is not closed yet */
    pthread_mutex_lock(&lock);
    Slot *q = slot_of(s, frame);
    if (q && q->state == PENDING) {
        q->state = picture ? READY : FAILED;
        q->picture = picture;
        picture = NULL;
    } else if (q && q->state == DROPPED)
        q->state = FREE;
    pthread_mutex_unlock(&lock);
    if (picture)
        s->backend->release(picture); /* dropped meanwhile, or a frame the decoder was never given */
}
/* Gives a decoded picture to write() and frees its slot; PTA_AGAIN while the decoder still has the frame. */
int pt_video_fetch(uint32_t session, uint32_t frame, PtVideoWrite write, void *context) {
    Session *gone[PTV_SESSIONS] = {0};
    int rc = PTA_CANCELLED;
    pthread_mutex_lock(&lock);
    Session *s = find(session, gone);
    if (s) {
        Slot *q = slot_of(s, frame);
        if (!q || q->state == DROPPED || !write)
            rc = PTA_INVALID;
        else if (q->state == PENDING)
            rc = PTA_AGAIN;
        else if (q->state == FAILED) {
            q->state = FREE;
            rc = PTA_DENIED;
        } else {
            uint32_t stride = 0;
            const uint8_t *rows = s->backend->pixels(q->picture, &stride);
            rc = rows && stride >= 2 * s->width ? write(context, rows, stride, s->width, s->height) : PTA_DENIED;
            if (rc == PTA_OK || rc == PTA_DENIED)
                vacate(s, q);
        }
    }
    pthread_mutex_unlock(&lock);
    end(gone);
    return rc;
}
/* Forgets a frame (or every frame, PTV_ALL) the guest will not fetch; one still decoding is freed when it is done. */
int pt_video_drop(uint32_t session, uint32_t frame) {
    Session *gone[PTV_SESSIONS] = {0};
    pthread_mutex_lock(&lock);
    Session *s = find(session, gone);
    for (unsigned k = 0; s && k < PTV_QUEUE; k++) {
        Slot *q = &s->slot[k];
        if (q->state == FREE || (frame != PTV_ALL && q->frame != frame))
            continue;
        if (q->state == PENDING)
            q->state = DROPPED;
        else if (q->state != DROPPED)
            vacate(s, q);
    }
    pthread_mutex_unlock(&lock);
    end(gone);
    return s ? PTA_OK : PTA_CANCELLED;
}
/* Ends a session: its decoder is closed outside the lock, its pictures freed. */
int pt_video_close(uint32_t session) {
    Session *gone[PTV_SESSIONS] = {0};
    int rc = PTA_CANCELLED;
    pthread_mutex_lock(&lock);
    if (find(session, gone))
        for (unsigned i = 0; i < PTV_SESSIONS; i++)
            if (table[i] && table[i]->id == session) {
                gone[i] = table[i];
                table[i] = NULL;
                rc = PTA_OK;
            }
    pthread_mutex_unlock(&lock);
    end(gone);
    return rc;
}

/* The stub decoder: no decoding, a picture that shows which frame it is. */
typedef struct {
    uint32_t width, height;
    void *owner;
} Stub;
typedef struct {
    uint32_t stride;
    uint8_t rows[];
} StubPicture;
/* The stub accepts any avcC of version 1. */
static void *stub_open(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, void *owner) {
    Stub *d = n >= 7 && avcc[0] == 1 ? malloc(sizeof *d) : NULL; /* configurationVersion */
    if (d)
        *d = (Stub){width, height, owner};
    return d;
}
/* Makes the frame's barcode picture at once, from inside submit (as a decoder may). */
static int stub_submit(void *o, uint32_t frame, const uint8_t *data, uint32_t n) {
    Stub *d = o;
    (void)data;
    (void)n;
    StubPicture *p = malloc(sizeof *p + (size_t)d->width * 2 * d->height);
    if (!p)
        return 1;
    p->stride = d->width * 2;
    for (uint32_t x = 0; x < d->width; x++) { /* 2vuy: Cb Y Cr Y */
        p->rows[2 * x] = 128;
        p->rows[2 * x + 1] = frame >> (31 - x * 32 / d->width) & 1 ? 235 : 16;
    }
    for (uint32_t y = 1; y < d->height; y++)
        memcpy(p->rows + (size_t)y * p->stride, p->rows, p->stride);
    pt_video_done(d->owner, frame, p);
    return 0;
}
/* A stub picture's rows. */
static const uint8_t *stub_pixels(void *picture, uint32_t *stride) {
    StubPicture *p = picture;
    *stride = p->stride;
    return p->rows;
}
/* Frees the stub decoder. */
static void stub_close(void *decoder) {
    free(decoder);
}
const PtVideoBackend pt_video_stub = {stub_open, stub_submit, stub_pixels, free, stub_close};
