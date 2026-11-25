/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Mac's decoder through pt_video.h, on real files: test-video-vt FILE.mp4 ...
 * The compressed frames are read with AVAssetReader (no decoding) and go through pt_video_decode / pt_video_fetch as
 * the guest's component will send them: in decoding order, several ahead, each under its number. The same file is
 * decoded by the system (AVAssetReader asked for 2vuy) and every picture of ours is compared, by its presentation
 * time, with the system's. Then: a seek (frames dropped while they decode, playing on from a later key frame), a
 * session made of garbage, a frame cut in half. Printed for each file: frames, equal pictures, frames per second. */
#import <AVFoundation/AVFoundation.h>
#include "pt_agent.h"
#include "pt_video.h"
#include <mach/mach_time.h>

static double seconds(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom * 1e-9;
}
static uint64_t hash(uint64_t h, const uint8_t *p, size_t n) { /* FNV-1a over 8-byte words: cheap beside a decode */
    for (size_t i = 0; i < n; i += 8) {
        uint64_t w = 0;
        memcpy(&w, p + i, n - i < 8 ? n - i : 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    return h;
}
static uint64_t picture;
static int take(void *context, const uint8_t *rows, uint32_t stride, uint32_t width, uint32_t height) {
    (void)context;
    picture = 0xcbf29ce484222325ull;
    for (uint32_t y = 0; y < height; y++)
        picture = hash(picture, rows + (size_t)y * stride, 2 * width);
    return PTA_OK;
}
/* PTA_OK with the picture's hash in `picture`, PTA_DENIED for a frame the decoder failed; anything else after 5 s */
static int fetch(uint32_t session, uint32_t frame) {
    double start = seconds();
    for (;;) {
        int rc = pt_video_fetch(session, frame, take, NULL);
        if (rc != PTA_AGAIN || seconds() - start > 5)
            return rc;
        usleep(200);
    }
}
static NSNumber *key(CMTime t) {
    return @(CMTimeConvertScale(t, 90000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value);
}
static int file(const char *path) {
    @autoreleasepool {
        AVAsset *asset = [AVAsset assetWithURL:[NSURL fileURLWithPath:@(path)]];
        AVAssetTrack *track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!track) {
            printf("%s: no video track\n", path);
            return 1;
        }
        /* the compressed frames, in decoding order */
        NSMutableArray<NSData *> *frames = [NSMutableArray array];
        NSMutableArray<NSNumber *> *times = [NSMutableArray array], *keys = [NSMutableArray array];
        NSData *avcc = nil;
        int width = 0, height = 0;
        unsigned skipped = 0; /* a buffer that is not exactly one frame: this test does not take it apart */
        NSError *error = nil;
        AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        AVAssetReaderTrackOutput *raw = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
        [reader addOutput:raw];
        [reader startReading];
        for (CMSampleBufferRef b; (b = [raw copyNextSampleBuffer]); CFRelease(b)) {
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(b);
            if (!block || CMSampleBufferGetNumSamples(b) != 1) {
                skipped += CMSampleBufferGetNumSamples(b) > 1;
                continue;
            }
            if (!avcc) {
                CMFormatDescriptionRef f = CMSampleBufferGetFormatDescription(b);
                NSDictionary *atoms = (__bridge NSDictionary *)CMFormatDescriptionGetExtension(
                        f, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
                avcc = atoms[@"avcC"];
                CMVideoDimensions d = CMVideoFormatDescriptionGetDimensions(f);
                width = d.width;
                height = d.height;
            }
            NSMutableData *bytes = [NSMutableData dataWithLength:CMBlockBufferGetDataLength(block)];
            CMBlockBufferCopyDataBytes(block, 0, bytes.length, bytes.mutableBytes);
            CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(b, false);
            BOOL dependent = attachments && CFArrayGetCount(attachments) &&
                             CFDictionaryGetValue(CFArrayGetValueAtIndex(attachments, 0),
                                                  kCMSampleAttachmentKey_NotSync) == kCFBooleanTrue;
            [frames addObject:bytes];
            [times addObject:key(CMSampleBufferGetPresentationTimeStamp(b))];
            [keys addObject:@(!dependent)];
        }
        if (!avcc || !frames.count || reader.status == AVAssetReaderStatusFailed) {
            printf("%s: not read as H.264 with an avcC (%s)\n", path, reader.error.localizedDescription.UTF8String ?: "");
            return 1;
        }
        /* the system's own pictures, by presentation time */
        NSMutableDictionary<NSNumber *, NSNumber *> *reference = [NSMutableDictionary dictionary];
        reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        AVAssetReaderTrackOutput *decoded = [[AVAssetReaderTrackOutput alloc]
                initWithTrack:track
               outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_422YpCbCr8)}];
        [reader addOutput:decoded];
        [reader startReading];
        for (CMSampleBufferRef b; (b = [decoded copyNextSampleBuffer]); CFRelease(b)) {
            CVPixelBufferRef image = CMSampleBufferGetImageBuffer(b);
            if (!image)
                continue;
            CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
            (void)take(NULL, CVPixelBufferGetBaseAddress(image), (uint32_t)CVPixelBufferGetBytesPerRow(image),
                       (uint32_t)width, (uint32_t)height);
            CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
            reference[key(CMSampleBufferGetPresentationTimeStamp(b))] = @(picture);
        }
        /* The samples carry media times; the system's pictures come on the movie's timeline, moved by the edit list (Big
         * Buck Bunny 480p: the first sample at 512/15360, its picture at 0). Compared by the raw times, every picture of
         * ours met the system's next one: 0 equal, 1 without a reference (u209, u209e). Both sides by one shift. */
        int64_t first_sample = INT64_MAX, first_picture = INT64_MAX;
        for (NSNumber *t in times)
            first_sample = MIN(first_sample, t.longLongValue);
        for (NSNumber *t in reference)
            first_picture = MIN(first_picture, t.longLongValue);
        /* and onto the nearest picture time within 1 ms: the two timelines round to 90 kHz apart (Jellyfish: 2 of 300) */
        for (NSUInteger k = 0; k < times.count && reference.count; k++) {
            int64_t t = times[k].longLongValue - first_sample + first_picture, best = t, gap = 91;
            for (NSNumber *r in reference)
                if (llabs(r.longLongValue - t) < gap) {
                    gap = llabs(r.longLongValue - t);
                    best = r.longLongValue;
                }
            times[k] = @(best);
        }
        /* 1. the whole stream, eight frames ahead of the one fetched */
        uint32_t session = 0;
        int rc = pt_video_open(avcc.bytes, (uint32_t)avcc.length, (uint32_t)width, (uint32_t)height, &session);
        if (rc) {
            printf("%s: %dx%d refused at open (%d)\n", path, width, height, rc);
            return 1;
        }
        unsigned n = (unsigned)frames.count, equal = 0, different = 0, failed = 0, missing = 0, bad = 0;
        double start = seconds();
        for (unsigned sent = 0, shown = 0; shown < n;) {
            if (sent < n && sent < shown + 8) {
                rc = pt_video_decode(session, sent + 1, frames[sent].bytes, (uint32_t)frames[sent].length);
                if (rc == PTA_OK) {
                    sent++;
                    continue;
                }
                if (rc != PTA_AGAIN) {
                    printf("%s: frame %u refused at decode (%d)\n", path, sent + 1, rc);
                    return 1;
                }
            }
            rc = fetch(session, shown + 1);
            NSNumber *r = reference[times[shown]];
            if (rc == PTA_DENIED)
                failed++;
            else if (rc != PTA_OK)
                bad++;
            else if (!r)
                missing++;
            else if (r.unsignedLongLongValue == picture)
                equal++;
            else
                different++;
            shown++;
        }
        double fps = n / (seconds() - start);
        /* 2. a seek: eight frames submitted and dropped at once, then on from the last key frame of the first half */
        unsigned from = 0, after = 0, after_equal = 0;
        for (unsigned i = 1; i < n / 2; i++)
            if (keys[i].boolValue)
                from = i;
        for (unsigned i = 0; i < 8 && i < n; i++)
            (void)pt_video_decode(session, 100000 + i, frames[i].bytes, (uint32_t)frames[i].length);
        rc = pt_video_drop(session, PTV_ALL);
        for (unsigned i = from; from && !rc && i < n && i < from + 60; i++) {
            do
                rc = pt_video_decode(session, 200000 + i, frames[i].bytes, (uint32_t)frames[i].length);
            while (rc == PTA_AGAIN && !usleep(200));
            if (!rc && !(rc = fetch(session, 200000 + i))) {
                after++;
                after_equal += reference[times[i]].unsignedLongLongValue == picture;
            }
        }
        int seek_ok = !from || (!rc && after && after == after_equal);
        /* 3. a frame cut in half: whatever the decoder makes of it, the session lives on and the next key frame is
         * right */
        int cut = PTA_OK, alive = 1;
        if (from) {
            cut = pt_video_decode(session, 300000, frames[from].bytes, (uint32_t)frames[from].length / 2);
            if (!cut)
                cut = fetch(session, 300000);
            rc = pt_video_decode(session, 300001, frames[from].bytes, (uint32_t)frames[from].length);
            alive = !rc && !fetch(session, 300001) && reference[times[from]].unsignedLongLongValue == picture;
        }
        int closed = pt_video_close(session);
        /* 4. a description that is no avcC */
        uint8_t garbage[64];
        memset(garbage, 0x5a, sizeof garbage);
        garbage[0] = 1;
        int junk = pt_video_open(garbage, sizeof garbage, (uint32_t)width, (uint32_t)height, &session);
        if (!junk) { /* accepted at open: then its frames must fail, and nothing may hang */
            junk = pt_video_decode(session, 1, frames[0].bytes, (uint32_t)frames[0].length);
            if (!junk)
                junk = fetch(session, 1);
            (void)pt_video_close(session);
        }
        /* every picture the system shows has its equal; a sample the edit list leaves out (Jellyfish: 2) has none */
        int ok = equal == reference.count && equal + missing == n && !skipped && seek_ok && alive && !closed && junk != PTA_OK && (height < 1080 || fps >= 120);
        printf("%s %s: %dx%d, %u frames: %u equal to the system's, %u different, %u failed, %u without a reference, %u "
               "not fetched, %u buffers skipped; %.0f frames/s; seek to frame %u: %u of %u equal; cut frame: %d, session alive: %d; garbage "
               "avcC: %d\n",
               ok ? "ok  " : "FAIL", path, width, height, n, equal, different, failed, missing, bad, skipped, fps, from + 1,
               after_equal, after, cut, alive, junk);
        return !ok;
    }
}
int main(int argc, char **argv) {
    int failures = 0;
    for (int i = 1; i < argc; i++)
        failures += file(argv[i]);
    if (argc < 2)
        puts("usage: test-video-vt FILE.mp4 ...");
    return failures || argc < 2;
}
