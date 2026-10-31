/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_AUDIO_H
#define PT_AUDIO_H
#include "dma.h"
enum { PT_PCM_CAPACITY=4096,PT_PCM_RATE=44100 };
/* Host FIFO between the guest play head (producer, CPU owner) and CoreAudio (sole consumer, real-time).
 * The consumer waits until `latency` frames are queued (priming) and primes again after it runs dry;
 * nothing it does reaches the guest's timeline. */
typedef struct {
    float samples[PT_PCM_CAPACITY][2];
    _Atomic uint32_t read,write,latency,primed,stopped; /* stopped: PH halted, running dry is the stream's end */
    _Atomic uint64_t frames,underruns,missing_frames,callbacks,dropped,tails; _Atomic uint32_t max_request;
} PtPcm;
void pt_pcm_init(PtPcm *,unsigned latency_frames);
unsigned pt_pcm_queued(PtPcm *);
int pt_pcm_push(PtPcm *,float left,float right); /* producer; 0 = full (the frame is dropped and counted) */
void pt_pcm_render(PtPcm *,float *interleaved,unsigned frames); /* sole consumer, RT safe */
void pt_pcm_drain(PtPcm *); /* consumer side, only when no AudioUnit consumes */
typedef struct PtAudioHost PtAudioHost;
/* Open/close on a control thread, NEVER from MMIO or the render callback. */
PtAudioHost *pt_audio_open(PtPcm *,int32_t *status);
int32_t pt_audio_close(PtAudioHost *);
/* One play head (PH). It advances at the guest rate on the machine clock from the
 * RUN write; the DMA copy, descriptor completion and its interrupt, and the frame counter (0x50) all follow it.
 * The host FIFO only adds latency: underruns and overflows never move PH. */
typedef struct {
    unsigned native_running,native_servicing,native_host_drift; uint64_t native_host_phase; float native_gain[2];
    PtDma *dma; PtPcm *pcm;
    uint32_t regs[6]; uint16_t codec[8];
    uint64_t dma_reset,ph_ns,ph_frac;     /* ph_ns 0: PH not running */
    unsigned rate,hold,partial;           /* hold: host-rate accumulator of the zero-order hold */
    int32_t trim_ppm; uint32_t level_ema; /* Approximation: trim by the FIFO level, not the DAC rate (AudioTimeStamps) */
    uint8_t sample[4]; float held[2];
    uint64_t consumed,starved,skipped,now; /* now: the time of the frame being consumed (IRQ timestamps) */
    int peak[2]; /* the loudest |sample| since the last stats line, as the guest wrote it and after the codec gain/mute (x32768) */
    void *dump; /* PT_AUDIO_DUMP: the guest's 16-bit frames as PH consumes them (raw LE stereo), for tests */
} PtScreamer;
void pt_screamer_init(PtScreamer *,PtDma *,PtPcm *);
/* Register access at now_ns: PH is brought up to now first, so 0x50 and DMA state are those of that instant. */
uint32_t pt_screamer_read(PtScreamer *,unsigned offset,uint64_t now_ns);
void pt_screamer_write(PtScreamer *,unsigned offset,uint32_t,uint64_t now_ns);
/* CPU owner: consume the guest frames due by now_ns. Call at <=1 ms cadence while TX DMA runs. */
void pt_screamer_service(PtScreamer *,uint64_t now_ns);
#endif
