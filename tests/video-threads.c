/* SPDX-License-Identifier: GPL-2.0-or-later */
/* src/video_guest.c under threads (ThreadSanitizer): one thread plays a stream - submits, fetches, reopens a lost
 * session - while another drops its frames and closes its session, and the decoder completes frames from a thread
 * of its own and, for some, from inside submit. No deadlock (alarm), no race, every picture released. */
#include "pt_agent.h"
#include "pt_video.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
const PtVideoBackend *pt_video_macos(void) { return NULL; } /* as in video-guest.c: this test brings its own decoder */
#endif

enum { W = 16, H = 4, FRAMES = 3000 };
typedef struct {
    void *owner;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    uint32_t queue[PTV_QUEUE + 1];
    unsigned n;
    int stop;
} Decoder;
static atomic_uint made, released, session;
static void complete(Decoder *d, uint32_t frame) {
    uint8_t *p = frame % 11 == 5 ? NULL : malloc(2 * W * H);
    if (p) {
        memset(p, (int)(frame & 255), 2 * W * H);
        atomic_fetch_add(&made, 1);
    }
    pt_video_done(d->owner, frame, p);
}
static void *worker(void *o) {
    Decoder *d = o;
    pthread_mutex_lock(&d->lock);
    while (!d->stop || d->n) {
        if (!d->n) {
            pthread_cond_wait(&d->wake, &d->lock);
            continue;
        }
        uint32_t frame = d->queue[0];
        memmove(d->queue, d->queue + 1, --d->n * sizeof *d->queue);
        pthread_mutex_unlock(&d->lock);
        usleep(50);
        complete(d, frame);
        pthread_mutex_lock(&d->lock);
    }
    pthread_mutex_unlock(&d->lock);
    return NULL;
}
static void *t_open(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, void *owner) {
    (void)avcc;
    (void)n;
    assert(width == W && height == H);
    Decoder *d = calloc(1, sizeof *d);
    d->owner = owner;
    pthread_mutex_init(&d->lock, NULL);
    pthread_cond_init(&d->wake, NULL);
    assert(!pthread_create(&d->thread, NULL, worker, d));
    return d;
}
static int t_submit(void *o, uint32_t frame, const uint8_t *data, uint32_t n) {
    Decoder *d = o;
    (void)data;
    (void)n;
    if (frame % 7 == 3) {
        complete(d, frame); /* from inside submit */
        return 0;
    }
    pthread_mutex_lock(&d->lock);
    assert(d->n <= PTV_QUEUE);
    d->queue[d->n++] = frame;
    pthread_cond_signal(&d->wake);
    pthread_mutex_unlock(&d->lock);
    return 0;
}
static const uint8_t *t_pixels(void *picture, uint32_t *stride) {
    *stride = 2 * W;
    return picture;
}
static void t_release(void *picture) {
    atomic_fetch_add(&released, 1);
    free(picture);
}
static void t_close(void *o) {
    Decoder *d = o;
    pthread_mutex_lock(&d->lock);
    d->stop = 1;
    pthread_cond_signal(&d->wake);
    pthread_mutex_unlock(&d->lock);
    pthread_join(d->thread, NULL);
    free(d);
}
static const PtVideoBackend threaded = {t_open, t_submit, t_pixels, t_release, t_close};
static unsigned shown;
static int show(void *context, const uint8_t *rows, uint32_t stride, uint32_t width, uint32_t height) {
    uint32_t frame = *(uint32_t *)context;
    assert(stride == 2 * W && width == W && height == H && rows[0] == (frame & 255) && rows[2 * W * H - 1] == (frame & 255));
    shown++;
    return PTA_OK;
}
static atomic_int finished;
static void *spoiler(void *o) {
    (void)o;
    for (unsigned i = 0; !atomic_load(&finished); i++) {
        usleep(700);
        uint32_t s = atomic_load(&session);
        if (i % 5 == 4)
            (void)pt_video_close(s);
        else
            (void)pt_video_drop(s, i % 2 ? PTV_ALL : i);
    }
    return NULL;
}
int main(void) {
    static const uint8_t avcc[8] = {1};
    uint8_t data[32] = {0};
    alarm(120);
    pt_video_backend(&threaded);
    pthread_t t;
    assert(!pthread_create(&t, NULL, spoiler, NULL));
    unsigned reopened = 0, again = 0;
    uint32_t s = 0, oldest = 0;
    for (uint32_t frame = 0; frame < FRAMES;) {
        int rc = s ? pt_video_decode(s, frame, data, sizeof data) : PTA_CANCELLED;
        if (rc == PTA_CANCELLED) {
            assert(pt_video_open(avcc, sizeof avcc, W, H, &s) == PTA_OK);
            atomic_store(&session, s);
            reopened++;
            oldest = frame;
            continue;
        }
        assert(rc == PTA_OK || rc == PTA_AGAIN || rc == PTA_INVALID); /* INVALID: its number is still a dropped slot */
        if (rc == PTA_OK)
            frame++;
        else
            again++;
        /* show what is ready, in order; a frame that is not there (dropped, failed) is passed over */
        while (oldest < frame) {
            rc = pt_video_fetch(s, oldest, show, &oldest);
            if (rc == PTA_AGAIN && frame - oldest < PTV_QUEUE / 2)
                break;
            if (rc == PTA_AGAIN) {
                usleep(20);
                continue;
            }
            oldest++;
        }
    }
    atomic_store(&finished, 1);
    pthread_join(t, NULL);
    (void)pt_video_close(s);
    assert(atomic_load(&made) == atomic_load(&released) && shown && reopened > 1);
    printf("video-threads: %u frames, %u shown, %u sessions, %u pictures made and released, %u waits\n", (unsigned)FRAMES,
           shown, reopened, atomic_load(&made), again);
    return 0;
}
