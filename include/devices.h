/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_DEVICES_H
#define PT_DEVICES_H
#include "ata.h"
#include "input.h"
#include "ohci.h"
#include "usb_msc.h"
#include "audio.h"
#include "pmu.h"
typedef struct PtDevices PtDevices;
typedef struct { PtDevices *owner; unsigned index; } PtDeviceLine;
struct PtDevices {
    int native_usb_clock;
    PtInputQueue input;PtPic *pic; PtGpio gpio; PtPmu pmu; uint64_t origin;
    PtDma dma[32]; PtDeviceLine dma_lines[32],ata_lines[2];
    PtAta ata[2]; PtBlock *block; PtScsi optical;
    PtUsbMsc storage;PtHid hid[3]; PtOhci usb; PtPcm pcm; PtScreamer sound; PtAudioHost *audio;
    int service_requested; uint32_t unbound_dma;
    uint64_t usb_at,service_at,skipped_usb_frames;
    uint64_t audio_last_ns,audio_max_gap_ns,audio_gaps_5ms; /* service gaps while PCM plays */
    uint64_t level_sum,level_n,irq_last,irq_n,irq_sum,irq_max; uint32_t level_min,level_max; /* play-head (PH) stats */
};
/* Disk is deliberately read-only in this harness. Write-capable ATA is tested
 * on private fixtures; production snapshot selection is a separate caller. */
int pt_devices_init(PtDevices *,PtRam,PtBus *,PtPic *,PtEvent *,
                    _Atomic uint32_t *kick,const char *raw,int audio,uint64_t now_ns,uint32_t unix_seconds);
/* CPU must be stopped and input producers detached. Close drains all owned
 * block workers; buffers, wake and kick stay alive until this returns.
 * On failure retain this object and its wake/kick storage until process exit. */
/* Writable system disk (a cloned snapshot), before the CPU runs and only when init got no disk. */
int pt_devices_disk(PtDevices *,PtBlock *,_Atomic uint32_t *kick);
int pt_devices_close(PtDevices *);
/* CPU owner attaches a prepared backend. Success transfers ownership to
 * PtDevices and returns a retired backend to close OFF the CPU thread.
 * Failure leaves ownership with caller. Each backend has one controller. */
int pt_devices_storage(PtDevices *,PtBlock *,int read_only,PtBlock **old);
int pt_devices_optical(PtDevices *,PtBlock *,PtBlock **old);
/* Fixed logical macio offsets; board PCI routing supplies the physical BAR. */
int pt_devices_macio(PtDevices *,uint32_t offset,uint8_t *,unsigned,int write,uint64_t now_ns);
void pt_devices_service(PtDevices *,uint64_t now_ns);
uint64_t pt_devices_deadline(PtDevices *);
void pt_devices_audio_stats(PtDevices *); /* one line of audio deltas since the last call (PT_STATS) */
#endif
