/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Mac's camera, through AVFoundation, as the USB camera's picture source. A capture session exists only while
 * the guest has the video stream open, so the Mac's camera light is on only then; pictures are kept in YUY2. */
#include "camera_macos.h"
#import <AVFoundation/AVFoundation.h>
#include <os/lock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* nd_lock: what the guest wants (set by the CPU owner) and the newest picture (written by the capture queue). */
static os_unfair_lock nd_lock = OS_UNFAIR_LOCK_INIT;
static int nd_wanted;
static unsigned nd_width, nd_height;
static uint8_t *nd_picture;
static uint64_t nd_serial, nd_taken;
/* nd_queue: the capture session and every change of it; nd_frame_queue: its frames (stopping a session must not wait
 * behind the queue its frames arrive on). */
static dispatch_queue_t nd_queue, nd_frame_queue;
static AVCaptureSession *nd_session;
static unsigned nd_session_width, nd_session_height;

@interface PtCameraFrames : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@end
@implementation PtCameraFrames
/* A captured frame, YUY2 ('yuvs') at the size asked for or larger: its centre becomes the newest picture. */
- (void)captureOutput:(AVCaptureOutput *)output
        didOutputSampleBuffer:(CMSampleBufferRef)sample
               fromConnection:(AVCaptureConnection *)connection {
    (void)output;
    (void)connection;
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image || CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_422YpCbCr8_yuvs)
        return;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    const uint8_t *from = CVPixelBufferGetBaseAddress(image);
    size_t w = CVPixelBufferGetWidth(image), h = CVPixelBufferGetHeight(image), line = CVPixelBufferGetBytesPerRow(image);
    os_unfair_lock_lock(&nd_lock);
    if (from && nd_picture && nd_wanted && w >= nd_width && h >= nd_height && line >= w * 2) {
        size_t x = ((w - nd_width) / 2) & ~(size_t)1, y = (h - nd_height) / 2; /* an even x keeps Y0 U Y1 V in phase */
        for (unsigned row = 0; row < nd_height; row++)
            memcpy(nd_picture + (size_t)row * nd_width * 2, from + (y + row) * line + x * 2, (size_t)nd_width * 2);
        nd_serial++;
    }
    os_unfair_lock_unlock(&nd_lock);
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
@end
static PtCameraFrames *nd_frames;

/* nd_queue: makes the session match what the guest wants - none, or one that delivers its frame size. */
static void nd_apply(void) {
    os_unfair_lock_lock(&nd_lock);
    int wanted = nd_wanted;
    unsigned w = nd_width, h = nd_height;
    os_unfair_lock_unlock(&nd_lock);
    if (nd_session && (!wanted || w != nd_session_width || h != nd_session_height)) {
        [nd_session stopRunning];
        nd_session = nil;
        fprintf(stderr, "ptolemy: camera: the Mac's camera is off\n");
    }
    if (!wanted || nd_session)
        return;
    AVAuthorizationStatus access = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (access == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     (void)granted;
                                     dispatch_async(nd_queue, ^{
                                         nd_apply();
                                     });
                                 }];
        return;
    }
    AVCaptureDevice *device = access == AVAuthorizationStatusAuthorized
                                      ? [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo]
                                      : nil;
    NSError *error = nil;
    AVCaptureDeviceInput *input = device ? [AVCaptureDeviceInput deviceInputWithDevice:device error:&error] : nil;
    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
    output.alwaysDiscardsLateVideoFrames = YES;
    @try { /* scaled to the guest's frame, cropped to its shape */
        output.videoSettings = @{
            (__bridge id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_422YpCbCr8_yuvs),
            (__bridge id)kCVPixelBufferWidthKey : @(w),
            (__bridge id)kCVPixelBufferHeightKey : @(h),
            AVVideoScalingModeKey : AVVideoScalingModeResizeAspectFill
        };
    } @catch (NSException *refused) { /* the camera's own size: the delegate takes its centre */
        (void)refused;
        output.videoSettings = @{(__bridge id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_422YpCbCr8_yuvs)};
    }
    if (!nd_frames)
        nd_frames = [[PtCameraFrames alloc] init];
    [output setSampleBufferDelegate:nd_frames queue:nd_frame_queue];
    if (!input || ![session canAddInput:input] || ![session canAddOutput:output]) {
        fprintf(stderr, "ptolemy: camera: no pictures from the Mac's camera (%s)\n",
                access != AVAuthorizationStatusAuthorized ? "no access: System Settings, Privacy & Security, Camera"
                : error                                   ? error.localizedDescription.UTF8String
                                                          : "no camera");
        return;
    }
    [session addInput:input];
    [session addOutput:output];
    [session startRunning];
    nd_session = session;
    nd_session_width = w;
    nd_session_height = h;
    fprintf(stderr, "ptolemy: camera: the Mac's camera is on, %ux%u\n", w, h);
}
static void nd_stream(void *opaque, int on, unsigned width, unsigned height) {
    (void)opaque;
    os_unfair_lock_lock(&nd_lock);
    nd_wanted = on;
    if (on) {
        uint8_t *picture = realloc(nd_picture, (size_t)width * height * 2);
        if (picture) {
            nd_picture = picture;
            nd_width = width;
            nd_height = height;
        } else
            nd_wanted = 0;
        nd_taken = nd_serial; /* nothing captured before this opening is shown */
    }
    os_unfair_lock_unlock(&nd_lock);
    dispatch_async(nd_queue, ^{
        nd_apply();
    });
}
static int nd_frame(void *opaque, unsigned width, unsigned height, uint8_t *yuy2) {
    (void)opaque;
    int got = 0;
    os_unfair_lock_lock(&nd_lock);
    if (nd_picture && nd_width == width && nd_height == height && nd_serial != nd_taken) {
        memcpy(yuy2, nd_picture, (size_t)width * height * 2);
        nd_taken = nd_serial;
        got = 1;
    }
    os_unfair_lock_unlock(&nd_lock);
    return got;
}
PtUvcSource pt_camera_macos_source(void) {
    if (!nd_queue) {
        nd_queue = dispatch_queue_create("org.ptolemy.camera", DISPATCH_QUEUE_SERIAL);
        nd_frame_queue = dispatch_queue_create("org.ptolemy.camera.frames", DISPATCH_QUEUE_SERIAL);
    }
    return (PtUvcSource){NULL, nd_stream, nd_frame};
}
