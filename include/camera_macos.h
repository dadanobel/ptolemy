/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_CAMERA_MACOS_H
#define PT_CAMERA_MACOS_H
#include "usb_uvc.h"
/* The Mac's camera as the USB camera's source of pictures (AVFoundation). The camera runs only between the guest's
 * opening and closing of the video stream: outside it there is no capture session and the Mac's indicator is off.
 * The first opening asks the user for access to the camera; without it, or without a camera, there are no pictures. */
PtUvcSource pt_camera_macos_source(void);
#endif
