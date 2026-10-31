/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_BLOCK_H
#define PT_BLOCK_H
#include "platform.h"
typedef struct PtBlock PtBlock;
enum { PT_BLOCK_READ, PT_BLOCK_WRITE, PT_BLOCK_FLUSH };
typedef struct {
    uint64_t id, generation, offset;
    void *buffer;
    size_t size;
    int operation, error;
} PtBlockRequest;
/* raw only. writable must only be used for a separate working snapshot.
 * The caller keeps buffer allocated and untouched until take() or close().
 * Use private staging buffers: the guest can still touch its own RAM.
 * Completion does NOT write guest descriptors or raise the device IRQ:
 * the CPU owner checks generation, then publishes data -> status -> IRQ. */
PtBlock *pt_block_open(const char *path, int writable, PtEvent *completion);
/* path may also be an unencrypted sparse bundle (hdiutil -type SPARSEBUNDLE, backing-store version 1):
 * only written bands exist on the host, and `hdiutil compact` returns the bands the guest freed.
 * Overlay works on it too (no save); open/convert/compact never run while the image is attached. */
/* writable == PT_BLOCK_OVERLAY: open a regular raw file read-only and map it
 * MAP_PRIVATE. Guest writes stay in process memory (kernel copy-on-write) and
 * never reach the file; FLUSH is a no-op. All overlays of a process share
 * PT_OVERLAY_MAX_MB (default 1024) of distinct written pages; a write that
 * would exceed it fails whole with EIO before any data changes. */
enum { PT_BLOCK_OVERLAY = 2 };
/* Overlay only, before the first submit. On close: clonefile() the source to
 * a unique temporary name, write every page the guest wrote, then publish it
 * as path with RENAME_EXCL; an existing path is never replaced. */
int pt_block_set_save(PtBlock *, const char *path);
/* Read-only optical media, with a virtual 32 KiB APM before bare HFS/HFS+.
 * Already partitioned and ISO9660 media pass through unchanged. Capacity and
 * request offsets describe the virtual disc; the source file is never edited.
 * Open on the host/media worker, before publishing the backend to the CPU. */
PtBlock *pt_block_open_optical(const char *path, PtEvent *completion);
uint64_t pt_block_size(const PtBlock *);
/* Install before the first submit. Cell must outlive close(). */
void pt_block_set_kick(PtBlock *,_Atomic uint32_t *);
/* Returns errno: 0, EAGAIN (bounded queue/contended short mutex), EINVAL... */
int pt_block_submit(PtBlock *, const PtBlockRequest *);
/* 1 = completed, 0 = none, negative errno = retry/error.
 * -EAGAIN means RETRY, never sleep waiting for another completion: the last
 * completion may already be queued behind the briefly contended mutex.
 * Result.error is a positive errno. Caller owns submit/take/close sequencing. */
int pt_block_take(PtBlock *, PtBlockRequest *);
/* Drain and join, discarding untaken results (not an implicit FLUSH CACHE).
 * Stop submissions first; event and buffers must remain alive.
 * Reset cannot undo a write already issued to the host. Do not reuse its
 * buffer just because the controller generation has changed.
 * Returns 0, or for an overlay: the errno of a failed save (nothing is
 * published), else ENOSPC if a write was refused by the page budget. */
int pt_block_close(PtBlock *);
#endif
