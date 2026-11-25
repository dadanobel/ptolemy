/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Mac's decoder behind pt_video.h: one VTDecompressionSession for a stream, made from the stream's avcC as it
 * is. Frames go in as the file has them (NAL units with lengths) and come out as 2vuy pictures, one for each frame,
 * in decoding order: showing them in the right order is the guest's QuickTime's work. */
#include "pt_video.h"
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    VTDecompressionSessionRef session;
    CMVideoFormatDescriptionRef format;
    void *owner;
    uint32_t width, height;
} Decoder;
/* VideoToolbox's thread, or the submitting one. The picture is handed over locked for reading. */
static void output(void *o, void *frame, OSStatus status, VTDecodeInfoFlags flags, CVImageBufferRef image, CMTime time,
                   CMTime duration) {
    Decoder *d = o;
    (void)time;
    (void)duration;
    if (status || !image || (flags & kVTDecodeInfo_FrameDropped) ||
        CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_422YpCbCr8 ||
        CVPixelBufferGetWidth(image) < d->width || CVPixelBufferGetHeight(image) < d->height ||
        CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly))
        image = NULL;
    else
        CFRetain(image);
    pt_video_done(d->owner, (uint32_t)(uintptr_t)frame, image);
}
/* A Core Foundation dictionary of one key and value. */
static CFDictionaryRef dictionary(const void *key, const void *value) {
    return CFDictionaryCreate(NULL, &key, &value, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}
/* Makes the session for a stream from its avcC, asking for 2vuy pictures. NULL: VideoToolbox refused it, and the
 * guest's own decoder plays the film. */
static void *open_(const uint8_t *avcc, uint32_t n, uint32_t width, uint32_t height, void *owner) {
    Decoder *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->owner = owner;
    d->width = width;
    d->height = height;
    SInt32 fourcc = kCVPixelFormatType_422YpCbCr8;
    CFDataRef data = CFDataCreate(NULL, avcc, (CFIndex)n);
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &fourcc);
    CFDictionaryRef atoms = dictionary(CFSTR("avcC"), data);
    /* Beside the avcC, what AVFoundation reads from such a file and the guest's description does not carry: H.264's
     * chroma sites (chroma_sample_loc_type 0: left) and video range. Without them VideoToolbox drew 2vuy with other
     * chroma than the system's (u209f: luma equal, chroma up to 18 off in 5 % of the pixels). */
    const void *keys[] = {kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
                          kCMFormatDescriptionExtension_ChromaLocationTopField,
                          kCMFormatDescriptionExtension_ChromaLocationBottomField, kCMFormatDescriptionExtension_FullRangeVideo};
    const void *values[] = {atoms, kCMFormatDescriptionChromaLocation_Left, kCMFormatDescriptionChromaLocation_Left,
                            kCFBooleanFalse};
    CFDictionaryRef extensions = CFDictionaryCreate(NULL, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef pixels = dictionary(kCVPixelBufferPixelFormatTypeKey, number);
    VTDecompressionOutputCallbackRecord record = {output, d};
    OSStatus s = CMVideoFormatDescriptionCreate(NULL, kCMVideoCodecType_H264, (int32_t)width, (int32_t)height,
                                                extensions, &d->format);
    if (!s)
        s = VTDecompressionSessionCreate(NULL, d->format, NULL, pixels, &record, &d->session);
    CFRelease(pixels);
    CFRelease(extensions);
    CFRelease(atoms);
    CFRelease(number);
    CFRelease(data);
    if (s) {
        fprintf(stderr, "video: VideoToolbox refused a %ux%u stream (%d): the guest's own decoder plays it\n", width,
                height, (int)s);
        if (d->format)
            CFRelease(d->format);
        free(d);
        return NULL;
    }
    CFBooleanRef hardware = NULL;
    (void)VTSessionCopyProperty(d->session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL,
                                &hardware);
    fprintf(stderr, "video: %ux%u H.264, %s decoder\n", width, height,
            hardware == kCFBooleanTrue ? "hardware" : hardware ? "software" : "unknown");
    if (hardware)
        CFRelease(hardware);
    return d;
}
/* Hands one frame (NAL units with their lengths, as in the file) to VideoToolbox, decoded asynchronously; output()
 * reports it. Nonzero: refused. */
static int submit(void *o, uint32_t frame, const uint8_t *bytes, uint32_t n) {
    Decoder *d = o;
    CMBlockBufferRef block = NULL;
    CMSampleBufferRef sample = NULL;
    size_t size = n;
    OSStatus s = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, n, NULL, NULL, 0, n, kCMBlockBufferAssureMemoryNowFlag,
                                                    &block);
    if (!s)
        s = CMBlockBufferReplaceDataBytes(bytes, block, 0, n);
    if (!s)
        s = CMSampleBufferCreateReady(NULL, block, d->format, 1, 0, NULL, 1, &size, &sample);
    /* An error here: whether output() ran for this frame or not, video_guest.c frees its place. */
    if (!s)
        s = VTDecompressionSessionDecodeFrame(d->session, sample, kVTDecodeFrame_EnableAsynchronousDecompression,
                                              (void *)(uintptr_t)frame, NULL);
    if (sample)
        CFRelease(sample);
    if (block)
        CFRelease(block);
    return s != 0;
}
/* A decoded picture's rows (2vuy) and their stride; valid until release. */
static const uint8_t *pixels_(void *picture, uint32_t *stride) {
    *stride = (uint32_t)CVPixelBufferGetBytesPerRow(picture);
    return CVPixelBufferGetBaseAddress(picture);
}
/* Unlocks and lets go of a picture output() kept. */
static void release(void *picture) {
    CVPixelBufferUnlockBaseAddress(picture, kCVPixelBufferLock_ReadOnly);
    CFRelease(picture);
}
/* Waits for the frames in flight, then ends the session. */
static void close_(void *o) {
    Decoder *d = o;
    (void)VTDecompressionSessionWaitForAsynchronousFrames(d->session);
    VTDecompressionSessionInvalidate(d->session);
    CFRelease(d->session);
    CFRelease(d->format);
    free(d);
}
/* The VideoToolbox backend that video_guest.c uses on macOS. */
const PtVideoBackend *pt_video_macos(void) {
    static const PtVideoBackend b = {open_, submit, pixels_, release, close_};
    return &b;
}
