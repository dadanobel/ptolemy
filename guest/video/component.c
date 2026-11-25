/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ptolemy Video: a QuickTime 7 image decompressor for H.264 ('imdc' / 'avc1' / 'Ptol') that decodes nothing itself.
 * It is a sub-codec of QuickTime's Base Codec: every compressed frame goes to the host through the agent's hypercall
 * (include/pt_video.h) and the picture comes back into QuickTime's own 2vuy buffer. Where no host answers - another
 * machine, an older engine, PT_VIDEO=0 - it does not register, and Apple's decoder plays as before.
 * Every call is logged to /tmp/ptvideo.log while that file exists (touch it to start, remove it to stop). */
#include <QuickTime/QuickTime.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pt_agent.h"
#include "pt_video.h"

extern int ptagent_call(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t[3]); /* guest/agent/call.S */

enum { VERSION = 0x00010001 }; /* make-rsrc.py has the same */
typedef struct {
    ComponentInstance self, target, base;
    OSType **wanted; /* the one destination pixel format, as the Base Codec wants it listed */
    Handle avcc;
    uint32_t session, width, height, frames, length_size;
    uint32_t sent;         /* the newest frame given to the host: older ones may not follow it (decoding order) */
    int behind;            /* BeginBand could not send a frame without waiting: it and the next go from the decode thread */
    pthread_mutex_t order; /* sent, behind and the sending itself */
} Globals;
typedef struct { /* the Base Codec keeps one for each frame in its queue */
    uint32_t frame, bytes;
    int tried, submitted, fetched;
} Frame;

/* Appends one line (time, process, thread, message) to /tmp/ptvideo.log, only while that file exists. */
static void note(const char *format, ...) {
    int f = open("/tmp/ptvideo.log", O_WRONLY | O_APPEND);
    if (f < 0)
        return;
    char line[256];
    va_list a;
    int n = snprintf(line, sizeof line, "%llu %d %p ", (unsigned long long)mach_absolute_time(), (int)getpid(),
                     (void *)pthread_self());
    va_start(a, format);
    n += vsnprintf(line + n, sizeof line - (size_t)n, format, a);
    va_end(a);
    if (n > (int)sizeof line - 1)
        n = (int)sizeof line - 1;
    line[n++] = '\n';
    (void)write(f, line, (size_t)n);
    close(f);
}
/* One video call to the engine through the agent's hypercall: op, session, frame, a buffer and its length; up to
 * three words come back in reply. */
static int host(uint32_t op, uint32_t session, uint32_t frame, const void *p, uint32_t n, uint32_t reply[3]) {
    uint32_t unused[3];
    return ptagent_call(op, session, frame, (uint32_t)(uintptr_t)p, n, reply ? reply : unused);
}
/* Sleeps one millisecond: the wait between two tries of a full host queue. */
static void pause_ms(void) {
    struct timespec t = {0, 1000000};
    nanosleep(&t, NULL);
}
/* PTA_FAULT: the host met a page that is not there yet (or, for a picture, not writable yet). Touching it makes it so. */
static void touch(volatile char *p, uint32_t n, int writing) {
    for (uint32_t i = 0; i < n; i = i + 4096 < n || i == n - 1 ? i + 4096 : n - 1) { /* each page, and the last byte */
        char c = p[i];
        if (writing)
            p[i] = c;
    }
}
/* Does the host answer the video calls? On a machine that does not know the instruction this is SIGILL. */
static sigjmp_buf illegal;
static void on_illegal(int s) {
    (void)s;
    siglongjmp(illegal, 1);
}
static int host_answers(void) {
    struct sigaction mine, before;
    uint32_t r[3] = {0, 0, 0};
    volatile int yes = 0;
    memset(&mine, 0, sizeof mine);
    mine.sa_handler = on_illegal;
    sigemptyset(&mine.sa_mask);
    if (sigaction(SIGILL, &mine, &before))
        return 0;
    if (!sigsetjmp(illegal, 1))
        yes = ptagent_call(PTA_VIDEO_PROBE, 0, 0, 0, 0, r) == PTA_OK && r[0] == PTV_VERSION && r[1];
    sigaction(SIGILL, &before, NULL);
    return yes;
}
/* Opens the stream on the host from the avcC kept at Preflight; also after the host forgot the session. */
static int start(Globals *g) {
    uint32_t r[3] = {0, 0, 0}, n = (uint32_t)GetHandleSize(g->avcc);
    int rc = PTA_FAULT;
    for (unsigned tries = 0; rc == PTA_FAULT && tries < 3; tries++) {
        touch(*g->avcc, n, 0);
        rc = host(PTA_VIDEO_OPEN, 0, g->width << 16 | g->height, *g->avcc, n, r);
    }
    g->session = rc == PTA_OK ? r[0] : 0;
    note("open %ux%u avcC %u bytes: %d session %u", g->width, g->height, n, rc, g->session);
    return rc == PTA_OK;
}
/* Sends one compressed frame to the host, waiting while its queue is full, touching pages it could not read,
 * reopening a forgotten session once. Nonzero: the host has it. */
static int submit(Globals *g, uint32_t frame, const void *data, uint32_t n) {
    for (unsigned waits = 0, faults = 0, reopened = 0;;) {
        int rc = host(PTA_VIDEO_DECODE, g->session, frame, data, n, NULL);
        if (rc == PTA_AGAIN && waits++ < 500)
            pause_ms(); /* the queue is full until a picture is drawn or dropped */
        else if (rc == PTA_FAULT && faults++ < 3)
            touch((volatile char *)(uintptr_t)data, n, 0);
        else if (rc == PTA_CANCELLED && g->avcc && !reopened++ && start(g))
            ; /* a new engine, or the session idled out: the picture is wrong until the next key frame */
        else {
            if (rc)
                note("frame %u not taken: %d", frame, rc);
            return rc == PTA_OK;
        }
    }
}

/* OpenComponent: our storage, and an instance of Apple's Base Codec that does everything we do not. */
static ComponentResult on_open(Globals *none, ComponentInstance self) {
    (void)none;
    Globals *g = (Globals *)NewPtrClear(sizeof *g);
    if (!g)
        return memFullErr;
    pthread_mutex_init(&g->order, NULL);
    SetComponentInstanceStorage(self, (Handle)g);
    g->self = g->target = self;
    ComponentResult e = OpenADefaultComponent(decompressorComponentType, kBaseCodecType, &g->base);
    if (!e)
        e = ComponentSetTarget(g->base, self);
    note("Open %p: %ld", (void *)g, (long)e);
    return e;
}
/* CloseComponent: closes the host session and frees what on_open and preflight made. */
static ComponentResult on_close(Globals *g, ComponentInstance self) {
    (void)self;
    if (!g)
        return noErr;
    note("Close %p session %u after %u frames", (void *)g, g->session, g->frames);
    if (g->session)
        (void)host(PTA_VIDEO_CLOSE, g->session, 0, NULL, 0, NULL);
    if (g->base)
        CloseComponent(g->base);
    if (g->wanted)
        DisposeHandle((Handle)g->wanted);
    if (g->avcc)
        DisposeHandle(g->avcc);
    pthread_mutex_destroy(&g->order);
    DisposePtr((Ptr)g);
    return noErr;
}
/* ComponentSetTarget: calls that we pass on go to whoever captured us. */
static ComponentResult on_target(Globals *g, ComponentInstance target) {
    g->target = target;
    return noErr;
}
/* ComponentCanDo: the image codec calls this component answers itself. */
static ComponentResult can_do(Globals *g, short selector) {
    (void)g;
    switch (selector) {
    case kImageCodecGetCodecInfoSelect:
    case kImageCodecPreflightSelect:
    case kImageCodecInitializeSelect:
    case kImageCodecBeginBandSelect:
    case kImageCodecDecodeBandSelect:
    case kImageCodecDrawBandSelect:
    case kImageCodecEndBandSelect:
    case kImageCodecQueueStartingSelect:
    case kImageCodecQueueStoppingSelect:
        return 1;
    }
    return 0;
}
/* ImageCodecGetCodecInfo: the name and abilities QuickTime shows for this decoder. */
static ComponentResult codec_info(Globals *g, CodecInfo *info) {
    (void)g;
    if (!info)
        return paramErr;
    memset(info, 0, sizeof *info);
    info->typeName[0] = 15; /* a Pascal string */
    memcpy(info->typeName + 1, "H.264 (Ptolemy)", 15);
    info->version = 1;
    info->revisionLevel = 1;
    info->vendor = 'Ptol';
    info->decompressFlags = codecInfoDoes32;
    info->formatFlags = codecInfoDepth24;
    info->decompressionAccuracy = 128;
    info->decompressionSpeed = 200;
    info->minimumHeight = info->minimumWidth = 2;
    return noErr;
}
/* ImageCodecInitialize: tells the Base Codec how this sub-codec takes frames and pictures. */
static ComponentResult initialize(Globals *g, ImageSubCodecDecompressCapabilities *cap) {
    (void)g;
    cap->decompressRecordSize = sizeof(Frame);
    cap->canAsync = true;
    /* QuickTime 7, as Apple's own H.264 decoder answers it (QuickTimeH264.altivec, JVTDecoInitialize, disassembled):
     * pictures go into the buffer DrawBand is given; frames come in decoding order with their own display times and
     * are drawn in decoding order (byte 26) with decode smoothing (byte 27, where the record has it) - two fields of
     * QuickTime 7.1's header that the 10.4 SDK lacks; DecodeBand only for the frames that are not drawn, DrawBand for the
     * others. Without byte 26 QuickTime started B-pyramid films over at their key frame again and again (u214h:
     * 22 583 of 24 058 frames dropped; the same films without the pyramid played every frame). */
    if (cap->recordSize > (long)offsetof(ImageSubCodecDecompressCapabilities, baseCodecShouldCallDecodeBandForAllFrames)) {
        cap->subCodecIsMultiBufferAware = true;
        cap->subCodecSupportsOutOfOrderDisplayTimes = true;
        cap->subCodecSupportsScheduledBackwardsPlaybackWithDifferenceFrames = true;
        cap->baseCodecShouldCallDecodeBandForAllFrames = false;
        if (cap->recordSize > 26)
            ((UInt8 *)cap)[26] = true; /* subCodecSupportsDrawInDecodeOrder */
        if (cap->recordSize > 27)
            ((UInt8 *)cap)[27] = true; /* subCodecSupportsDecodeSmoothing */
    }
    note("Initialize record %ld", cap->recordSize);
    return noErr;
}
/* ImageCodecPreflight: the picture size, 2vuy as the only destination format, and the host stream opened from the
 * film's avcC. */
static ComponentResult preflight(Globals *g, CodecDecompressParams *p) {
    ImageDescriptionHandle d = p->imageDescription;
    CodecCapabilities *c = p->capabilities;
    g->width = (uint32_t)(*d)->width;
    g->height = (uint32_t)(*d)->height;
    c->bandMin = (*d)->height;
    c->bandInc = c->bandMin;
    c->wantedPixelSize = 0;
    c->extendWidth = c->extendHeight = 0;
    if (!g->wanted) {
        if (!(g->wanted = (OSType **)NewHandleClear(2 * sizeof(OSType))))
            return memFullErr;
        (*g->wanted)[0] = k422YpCbCr8PixelFormat;
    }
    p->wantedDestinationPixelTypes = g->wanted;
    if (g->session) /* Preflight comes again when the destination changes: the stream is the same */
        return noErr;
    if (!g->avcc) {
        OSErr e = GetImageDescriptionExtension(d, &g->avcc, 'avcC', 1);
        if (e || !g->avcc || GetHandleSize(g->avcc) < 7) {
            note("Preflight: no avcC (%d)", (int)e);
            return codecBadDataErr;
        }
        HLock(g->avcc);
        g->length_size = ((unsigned char)(*g->avcc)[4] & 3u) + 1;
    }
    /* An error here is what sends QuickTime to the next decompressor for 'avc1' (to be seen in the first run). */
    return start(g) ? noErr : codecErr;
}
static void send(Globals *g, ImageSubCodecDecompressRecord *drp, int early);
/* ImageCodecBeginBand: classifies the frame (key, reference, droppable) for QuickTime and tries to send it to the
 * host at once. */
static ComponentResult begin_band(Globals *g, CodecDecompressParams *p, ImageSubCodecDecompressRecord *drp, long flags) {
    Frame *f = (Frame *)drp->userDecompressRecord;
    const unsigned char *data = (const unsigned char *)drp->codecData;
    uint32_t n = (uint32_t)p->bufferSize, key = 0, reference = 0;
    /* The frame's kind from its NAL unit headers (ISO/IEC 14496-10, 7.3.1): type 5 is an IDR slice; a slice with
     * nal_ref_idc 0 is not used by any other frame. */
    for (uint32_t at = 0; at + g->length_size < n;) {
        uint32_t length = 0;
        for (uint32_t k = 0; k < g->length_size; k++)
            length = length << 8 | data[at + k];
        unsigned header = data[at + g->length_size], type = header & 31;
        key |= type == 5;
        reference |= type >= 1 && type <= 5 && (header & 0x60);
        if (length > n - at - g->length_size)
            break;
        at += g->length_size + length;
    }
    drp->frameType = key ? kCodecFrameTypeKey : reference ? kCodecFrameTypeDifference : kCodecFrameTypeDroppableDifference;
    f->frame = ++g->frames;
    f->bytes = n;
    f->tried = f->submitted = f->fetched = 0;
    send(g, drp, 1);
    note("BeginBand frame %u bytes %u kind %u number %ld flags %lx: %d", f->frame, n, (unsigned)drp->frameType,
         p->frameNumber, flags, f->submitted);
    return noErr;
}
/* A frame goes to the host as soon as that costs QuickTime no waiting, and always in decoding order. BeginBand runs
 * on QuickTime's own thread while it fills its queue (34 frames ahead in u214): there one try, no waiting - waiting on
 * a full host queue (PTV_QUEUE) kept that thread, and the decode thread behind it, still for a minute (u214). A frame
 * BeginBand cannot send, and every one after it, goes from DecodeBand/DrawBand on the decode thread, which may wait;
 * sent only at DecodeBand, a 1080p picture was not ready at DrawBand and the decode thread slept 44 % of the time
 * (u214c). A frame QuickTime never decodes after it was left to the decode thread is never sent. */
static void send(Globals *g, ImageSubCodecDecompressRecord *drp, int early) {
    Frame *f = (Frame *)drp->userDecompressRecord;
    if (early ? pthread_mutex_trylock(&g->order) : pthread_mutex_lock(&g->order)) {
        g->behind = 1; /* the decode thread is sending: this frame and the next are left to it. Safe without the lock:
                        * it clears behind only at a frame >= g->frames, which BeginBand raised to this one already */
        return;
    }
    if (!f->tried && !(early && g->behind)) {
        if (f->frame <= g->sent)
            f->tried = 1; /* a newer frame went first: this one cannot follow it; DrawBand keeps the last picture */
        else if (!early) {
            f->tried = 1;
            f->submitted = submit(g, f->frame, drp->codecData, f->bytes);
            g->sent = f->frame;
        } else {
            int rc = host(PTA_VIDEO_DECODE, g->session, f->frame, drp->codecData, f->bytes, NULL);
            if (rc == PTA_FAULT) { /* pages not there yet: touching them is no waiting */
                touch((volatile char *)drp->codecData, f->bytes, 0);
                rc = host(PTA_VIDEO_DECODE, g->session, f->frame, drp->codecData, f->bytes, NULL);
            }
            if (rc == PTA_OK) {
                f->tried = f->submitted = 1;
                g->sent = f->frame;
            } else
                g->behind = 1;
        }
    }
    if (!early && f->frame >= g->frames)
        g->behind = 0; /* the decode thread has reached the newest queued frame */
    pthread_mutex_unlock(&g->order);
}
/* ImageCodecDecodeBand: sends the frame if BeginBand could not (the decode thread may wait). */
static ComponentResult decode_band(Globals *g, ImageSubCodecDecompressRecord *drp, unsigned long flags) {
    send(g, drp, 0);
    note("DecodeBand frame %u flags %lx: %d", ((Frame *)drp->userDecompressRecord)->frame, flags,
         ((Frame *)drp->userDecompressRecord)->submitted);
    return noErr;
}
/* ImageCodecDrawBand: fetches the host's picture of this frame into QuickTime's buffer, waiting up to a second for
 * it. */
static ComponentResult draw_band(Globals *g, ImageSubCodecDecompressRecord *drp) {
    Frame *f = (Frame *)drp->userDecompressRecord;
    unsigned waits = 0, faults = 0;
    int rc = PTA_DENIED;
    send(g, drp, 0); /* a frame drawn without DecodeBand (QuickTime's first picture of a movie) */
    while (f->submitted) {
        rc = host(PTA_VIDEO_FETCH, g->session, f->frame, drp->baseAddr, (uint32_t)drp->rowBytes, NULL);
        if (rc == PTA_AGAIN && waits++ < 1000)
            pause_ms();
        else if (rc == PTA_FAULT && faults++ < 3)
            touch(drp->baseAddr, (g->height - 1) * (uint32_t)drp->rowBytes + 2 * g->width, 1);
        else
            break;
    }
    f->fetched = rc == PTA_OK; /* otherwise the buffer keeps what it had: one wrong picture, not a stopped film */
    note("DrawBand frame %u rowBytes %ld: %d after %u ms", f->frame, drp->rowBytes, rc, waits);
    return noErr;
}
/* ImageCodecEndBand: a frame sent but never fetched is dropped on the host, so its queue does not fill up. */
static ComponentResult end_band(Globals *g, ImageSubCodecDecompressRecord *drp, OSErr result, long flags) {
    Frame *f = (Frame *)drp->userDecompressRecord;
    if (f->submitted && !f->fetched)
        (void)host(PTA_VIDEO_DROP, g->session, f->frame, NULL, 0, NULL); /* a seek, or a frame QuickTime skipped */
    note("EndBand frame %u result %d flags %lx%s", f->frame, (int)result, flags,
         f->submitted && !f->fetched ? " dropped" : "");
    return noErr;
}
/* QueueStarting and QueueStopping: nothing to do but log. */
static ComponentResult queue(Globals *g) {
    note("Queue starting or stopping, %u frames so far", g->frames);
    return noErr;
}

/* The entry point named by the 'dlle' resource. The numbers are the Mixed Mode descriptions of each function's
 * arguments, as ImageCodec.k.h and Components.k.h give them for ComponentDispatchHelper.c; everything this
 * component does not do itself is the Base Codec's. */
pascal ComponentResult PtVideoDispatch(ComponentParameters *p, Handle storage) {
    Globals *g = (Globals *)storage;
    ProcPtr f = NULL;
    ProcInfoType how = 0x3f0; /* storage and one pointer */
    switch (p->what) {
    case kComponentOpenSelect: f = (ProcPtr)on_open; break;
    case kComponentCloseSelect: f = (ProcPtr)on_close; break;
    case kComponentTargetSelect: f = (ProcPtr)on_target; break;
    case kComponentVersionSelect: return VERSION;
    case kComponentRegisterSelect: { /* nonzero: do not register */
        int yes = host_answers();
        note("Register: the host %s", yes ? "answers" : "does not answer");
        return !yes;
    }
    case kComponentCanDoSelect:
        if (CallComponentFunctionWithStorageProcInfo(storage, p, (ProcPtr)can_do, 0x2f0))
            return 1;
        break;
    case kImageCodecGetCodecInfoSelect: f = (ProcPtr)codec_info; break;
    case kImageCodecInitializeSelect: f = (ProcPtr)initialize; break;
    case kImageCodecPreflightSelect: f = (ProcPtr)preflight; break;
    case kImageCodecBeginBandSelect: f = (ProcPtr)begin_band; how = 0x3ff0; break;
    case kImageCodecDecodeBandSelect: f = (ProcPtr)decode_band; how = 0xff0; break;
    case kImageCodecDrawBandSelect: f = (ProcPtr)draw_band; break;
    case kImageCodecEndBandSelect: f = (ProcPtr)end_band; how = 0x3bf0; break;
    case kImageCodecQueueStartingSelect:
    case kImageCodecQueueStoppingSelect: f = (ProcPtr)queue; how = 0xf0; break;
    }
    if (f)
        return CallComponentFunctionWithStorageProcInfo(storage, p, f, how);
    return g && g->base ? DelegateComponentCall(p, g->base) : badComponentSelector;
}
