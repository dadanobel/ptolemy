/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_STAND_WINDOW_H
#define PT_STAND_WINDOW_H
#include <stdatomic.h>
#include <pthread.h>
#include "input.h"
#include "pointer_mailbox.h"
#include "radeon.h"
/* The hardware cursor of the guest display driver (guest/ndrv), written by its hypercall on the CPU thread,
 * read by the window and screenshots. pos: x, y (int16, the image's top-left in guest pixels), bit 32 visible. image:
 * w x h (at most 64 x 64; 0 x 0 none), straight ARGB, 0x00ffffff inverts the pixel below; gen counts images.
 * gamma: the driver's gamma ramp, 0x00RRGGBB per 8-bit input level, applied to the whole output (cursor included)
 * when gamma_on (not the identity); gamma_gen counts ramps. Under lock, like the image. */
typedef struct { _Atomic uint64_t pos; pthread_mutex_t lock; uint32_t gen,w,h,image[64*64],gamma_gen,gamma[256]; int gamma_on; } PtHwCursor;
/* Window mode of the standalone board: AppKit on the main thread, machine() on its own thread.
 * machine() calls ready() once devices exist, and detach() before destroying them. The display is the Radeon frame lease. */
typedef struct { int (*machine)(void); PtInputQueue *input; _Atomic int *stop; _Atomic int *pause; PtRadeon **radeon; const char *agent_socket; PtCursorMailbox *cursor; struct PtAgent **agent;
    int (*media)(const char *path,int slot); PtHwCursor *hw_cursor; } PtStandWindow;   /* any thread but the CPU owner; path NULL ejects slot 0 CD/DVD, 1 USB disk */
/* Exit status of a machine that asked to restart (guest reset or the window's menu); in window mode main() re-executes. */
#define PT_EXIT_RESTART 95
int pt_stand_window_main(PtStandWindow *);
void pt_stand_window_ready(void);
/* CPU stopped, AppKit loop still running: detach input and drain the final Metal
 * submission on main before the CPU destroys devices/events or releases VRAM. */
void pt_stand_window_detach(void);
#endif
