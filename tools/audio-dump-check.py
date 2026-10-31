#!/usr/bin/env python3
"""Continuity check of a PT_AUDIO_DUMP stream (raw LE int16 stereo, the guest frames as the play head
consumed them) carrying guest/tests/pt-tone.c's 997 Hz sine. Tone segments are runs of 64-frame blocks with a peak above
half the expected amplitude; inside a segment every frame must follow the sine recurrence x[n]=2cos(w)x[n-1]-x[n-2]."""
import array, math, sys

x = array.array('h'); x.frombytes(open(sys.argv[1], 'rb').read())
if sys.byteorder == 'big': x.byteswap()
left = x[0::2]
c = 2 * math.cos(2 * math.pi * 997 / 44100)
B = 64
active = [max(map(abs, left[i:i + B]), default=0) > 400 for i in range(0, len(left), B)]
segments, start = [], None
for i, a in enumerate(active + [False]):
    if a and start is None: start = i
    if not a and start is not None: segments.append((start * B, i * B)); start = None
glitches, worst = 0, 0
for s, e in segments:
    last = -B
    for n in range(s + B + 2, e - B):
        r = abs(left[n] - (c * left[n - 1] - left[n - 2]))
        worst = max(worst, r)
        if r > 40:
            if n - last >= B: glitches += 1
            last = n
print('DUMP frames=%d seconds=%.1f segments=%d [%s] glitches=%d worst_residual=%.0f' % (
    len(left), len(left) / 44100, len(segments), ' '.join('%.2f' % ((e - s) / 44100) for s, e in segments[:12]),
    glitches, worst))
