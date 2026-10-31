/* Audio timeline probe for Tiger (built in the guest with the devkit's ppc-cc).
 * Plays a quiet 997 Hz sine (-30 dBFS; period exactly 44100 frames) through the default output for N seconds and
 * reports the rate the guest sees against mach_absolute_time and HAL sample-time gaps (IOProc overloads).
 * The host side checks the same stream for continuity with PT_AUDIO_DUMP + tools/audio-dump-check.py. */
#include <AudioUnit/AudioUnit.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile UInt64 frames, calls, gaps, gap_frames, t_first, t_last, f_first, f_last;
static Float64 next_sample = -1;

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts,
                       UInt32 bus, UInt32 n, AudioBufferList *io)
{
    float *p = io->mBuffers[0].mData;
    UInt64 now = mach_absolute_time();
    (void)ref; (void)flags; (void)bus;
    for (UInt32 i = 0; i < n; i++) {
        float v = 0.03f * (float)sin(2 * M_PI * 997.0 * (double)((frames + i) % 44100) / 44100.0);
        p[2 * i] = v; p[2 * i + 1] = v;
    }
    if ((ts->mFlags & kAudioTimeStampSampleTimeValid) && next_sample >= 0 && ts->mSampleTime != next_sample) {
        gaps++; gap_frames += (UInt64)fabs(ts->mSampleTime - next_sample);
    }
    next_sample = ts->mSampleTime + n;
    if (!calls) { t_first = now; f_first = frames; }
    t_last = now; f_last = frames;
    frames += n; calls++;
    return noErr;
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    ComponentDescription d = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
    Component c = FindNextComponent(NULL, &d);
    AudioUnit au;
    if (!c || OpenAComponent(c, &au)) { puts("TONE error=no-default-output"); return 1; }
    AudioStreamBasicDescription f = { 44100, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked, 8, 1, 8, 2, 32, 0 };
    AURenderCallbackStruct cb = { render, NULL };
    OSStatus e = AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    if (!e) e = AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (!e) e = AudioUnitInitialize(au);
    if (!e) e = AudioOutputUnitStart(au);
    if (e) { printf("TONE error=%ld\n", (long)e); return 1; }
    sleep(seconds);
    AudioOutputUnitStop(au);
    AudioUnitUninitialize(au);
    CloseComponent(au);
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    double span = (double)(t_last - t_first) * tb.numer / tb.denom / 1e9;
    printf("TONE seconds=%d calls=%llu frames=%llu rate=%.2f hal_gaps=%llu gap_frames=%llu\n", seconds,
           (unsigned long long)calls, (unsigned long long)frames, span > 0 ? (f_last - f_first) / span : 0.0,
           (unsigned long long)gaps, (unsigned long long)gap_frames);
    return 0;
}
