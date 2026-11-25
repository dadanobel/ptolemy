/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Video decoding for the guest's QuickTime component. The guest hands over a stream's description (avcC) and its
 * compressed frames in decoding order, each under a number of its own, and takes the pictures back by number, in any
 * order, as 2vuy. The decoder is the host's: nothing of H.264 is parsed here or in the guest.
 * Results are pt_agent.h's: PTA_OK; PTA_AGAIN - not ready or the queue is full, ask again; PTA_BUSY - no session left;
 * PTA_INVALID - arguments, an unknown or repeated frame; PTA_DENIED - video is off, or the decoder refused the stream
 * or the frame; PTA_CANCELLED - no such session (closed for being idle, or a new engine): open it again. */
#ifndef PT_VIDEO_H
#define PT_VIDEO_H
#include <stdint.h>
enum {
    PTV_VERSION = 1,
    PTV_SESSIONS = 4,
    PTV_QUEUE = 24, /* frames of a session submitted and not yet fetched or dropped */
    PTV_MAX_WIDTH = 4096,
    PTV_MAX_HEIGHT = 2304,
    PTV_IDLE_MS = 30000 /* a session nobody called for this long is closed */
};
#define PTV_MAX_FRAME (8u << 20) /* a compressed frame */
#define PTV_ALL 0xffffffffu /* drop: every frame of the session */

/* A decoder. open: NULL refuses the stream. submit: 0 - pt_video_done(owner, frame, picture) follows, once, from any
 * thread, also from inside submit, with picture NULL for a frame that failed; nonzero - the frame is refused.
 * pixels: the picture's 2vuy rows until release. close returns after the last pt_video_done. */
typedef struct {
    void *(*open)(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, void *owner);
    int (*submit)(void *decoder, uint32_t frame, const uint8_t *data, uint32_t n);
    const uint8_t *(*pixels)(void *picture, uint32_t *stride);
    void (*release)(void *picture);
    void (*close)(void *decoder);
} PtVideoBackend;
void pt_video_done(void *owner, uint32_t frame, void *picture);
/* PT_VIDEO=stub: every picture is 32 vertical bars, the frame's number in binary, the highest bit on the left. */
extern const PtVideoBackend pt_video_stub;
const PtVideoBackend *pt_video_macos(void); /* VideoToolbox (video_macos.c) */
/* The decoder is chosen at the first call: PT_VIDEO=0 none, stub, otherwise the Mac's. Tests set their own. */
void pt_video_backend(const PtVideoBackend *);
void pt_video_clock(uint64_t (*milliseconds)(void)); /* tests; NULL: the monotonic clock */

int pt_video_enabled(void);
int pt_video_open(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, uint32_t *session);
int pt_video_decode(uint32_t session, uint32_t frame, const uint8_t *data, uint32_t n);
/* A ready picture goes to write (rows of 2 * width bytes, stride apart) and leaves the queue when write answers PTA_OK
 * or PTA_DENIED; any other answer is returned and the picture stays. */
typedef int (*PtVideoWrite)(void *context, const uint8_t *rows, uint32_t stride, uint32_t width, uint32_t height);
int pt_video_fetch(uint32_t session, uint32_t frame, PtVideoWrite, void *context);
int pt_video_drop(uint32_t session, uint32_t frame);
int pt_video_close(uint32_t session);
#endif
