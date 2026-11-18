#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher art, regenerated with python3 launcher-art.py (in assets/; numpy + Pillow). Original procedural
textures, no source images: launcher-fur.jpg (tiger-like fur), launcher-leopard.jpg (leopard-like fur), launcher-metal.jpg
(brushed metal); mark-glass.png is
mark-transparent-1024.png turned into translucent dark glass with a light rim; tile-tiger.png and tile-leopard.png are
the list's tiles (tiles() below)."""

import sys

import numpy as np
from PIL import Image, ImageFilter


def tiles():
    """The list's tiles (tile-tiger.png, tile-leopard.png): the system's fur in a brushed-metal bezel, a rounded square
    with its own alpha, cut from the files this script makes. Original drawing: no system artwork, no letter, no logo.
    256 px, shown at 40 to 132 points. `python3 launcher-art.py tiles` makes only these two."""
    n, bezel, radius = 256, 13, 54
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)

    def rounded(inset, r):  # coverage of a rounded square inset from the edge, antialiased
        q = np.maximum(np.abs(np.stack([xx, yy]) - (n - 1) / 2) - ((n - 1) / 2 - inset - r), 0)
        return np.clip(r - np.hypot(q[0], q[1]) + 0.5, 0, 1)

    outer, inner = rounded(1, radius), rounded(bezel, radius - bezel)
    streak = np.asarray(Image.open('launcher-metal.jpg').convert('L').resize((n, n), Image.LANCZOS), np.float32) / 255
    rim = (0.50 + 0.42 * (1 - yy / n) + (streak - 0.5) * 0.35)[..., None] * np.array([0.97, 0.98, 1.0])
    shade = np.clip(1.08 - 0.55 * np.hypot(xx / n - 0.42, yy / n - 0.36), 0.55, 1.1)[..., None]  # light from above left
    groove = np.clip(rounded(bezel - 2.5, radius - bezel + 2.5) - inner, 0, 1)[..., None]  # the dark seam inside the bezel
    gloss = (np.clip(1 - yy / (n * 0.46), 0, 1) ** 1.6 * 0.20 * inner)[..., None]  # a sheet of light over the upper half
    for fur, name, crop in (
        ('launcher-fur.jpg', 'tile-tiger.png', (330, 150, 830, 650)),
        ('launcher-leopard.jpg', 'tile-leopard.png', (300, 200, 720, 620)),
    ):
        f = np.asarray(Image.open(fur).convert('RGB').crop(crop).resize((n, n), Image.LANCZOS), np.float32) / 255
        rgb = rim * (1 - inner[..., None]) + f * shade * inner[..., None]
        rgb = rgb * (1 - groove * 0.75) + gloss
        Image.fromarray((np.dstack([np.clip(rgb, 0, 1), outer]) * 255).astype(np.uint8)).save(name)


if sys.argv[1:] == ['tiles']:
    tiles()
    sys.exit()

W, H = 1200, 800
y, x = np.mgrid[0:H, 0:W].astype(np.float32)


def noise(scale, seed):
    r = np.random.default_rng(seed)
    g = r.random((H // scale + 3, W // scale + 3)).astype(np.float32)
    im = (
        Image.fromarray((g * 255).astype(np.uint8))
        .resize((W + 3 * scale, H + 3 * scale), Image.BICUBIC)
        .crop((scale, scale, W + scale, H + scale))
    )
    return np.asarray(im, np.float32) / 255


wa = noise(220, 1) * 2 - 1
wb = noise(80, 2) * 2 - 1
u = x + wa * 110 + wb * 22 + y * 0.18  # stripes run mostly up and down, gently curved
period = 92
phase = (u / period) % 1.0
thick = 0.08 + 0.14 * noise(120, 3)  # stripe width varies: tapers and forks
taper = np.clip(noise(55, 4) * 1.6 - 0.25, 0, 1)
r0 = np.random.default_rng(11).random((H, W)).astype(np.float32)
kk = np.ones(15) / 15
edge = np.apply_along_axis(lambda c: np.convolve(c, kk, mode='same'), 0, r0)
d = np.abs(phase - 0.5) + (edge - 0.5) * 0.05
breaks = np.clip((noise(60, 12) - 0.04) * 3.5, 0, 1)
stripe = np.clip((thick * taper - d) / 0.05, 0, 1) * breaks
# fibres: vertical streaks
r = np.random.default_rng(9).random((H, W)).astype(np.float32)
k = np.ones(9) / 9
fib = np.apply_along_axis(lambda c: np.convolve(c, k, mode='same'), 0, r)
fine = np.random.default_rng(10).random((H, W)).astype(np.float32)
light = 0.78 + 0.30 * noise(200, 5)
orange = np.array([0.95, 0.50, 0.12])[:, None, None]
col = orange * light * (0.80 + 0.28 * fib + 0.06 * fine)
belly = np.clip((noise(150, 6) - 0.70) * 2.5, 0, 1)[None] * 0.45
col = col * (1 - belly) + np.array([0.98, 0.86, 0.66])[:, None, None] * belly
dark = np.array([0.06, 0.04, 0.03])[:, None, None] * (0.8 + 0.4 * fib)
col = col * (1 - stripe) + dark * stripe
img = Image.fromarray((np.clip(col.transpose(1, 2, 0), 0, 1) * 255).astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(0.5)
)
img.save('launcher-fur.jpg', quality=86)
# brushed metal: horizontal streaks
rng = np.random.default_rng(7)
m = rng.random((400, 1200)).astype(np.float32) * 255
k = np.ones(61) / 61
st = np.apply_along_axis(lambda r: np.convolve(r, k, mode='same'), 1, m) / 255
metal = np.clip(np.linspace(0.62, 0.42, 400)[:, None] + (st - 0.5) * 0.55, 0, 1)
Image.fromarray((np.stack([metal * 0.97, metal * 0.98, metal], 2) * 255).astype(np.uint8)).save(
    'launcher-metal.jpg', quality=88
)
# the mark as dark glass
mk = Image.open('mark-transparent-1024.png').convert('RGBA')
mk = mk.crop(mk.getbbox())
mk.thumbnail((440, 440), Image.LANCZOS)
a = np.asarray(mk, np.float32) / 255
al = a[..., 3]
L = a[..., :3].mean(2)
prof = np.clip((L - 0.45) * 4, 0, 1) * al
inner = np.asarray(Image.fromarray((al * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(5)), np.float32) / 255
rim = np.clip(al - inner, 0, 1)
out = np.zeros(a.shape, np.float32)
stone = al * (1 - prof)
sheen = np.linspace(1.25, 0.75, a.shape[0])[:, None]
out[..., :3] = stone[..., None] * np.array([0.16, 0.17, 0.19]) * sheen[..., None]
out[..., 3] = stone * 0.62
out[..., :3] = out[..., :3] * (1 - rim[..., None]) + rim[..., None] * 0.85
out[..., 3] = np.maximum(out[..., 3], rim * 0.75)
out[..., :3] = out[..., :3] * (1 - prof[..., None]) + prof[..., None] * 0.95
out[..., 3] = np.maximum(out[..., 3], prof)
alpha = np.clip(out[..., 3], 1e-4, 1)
rgb = np.clip(out[..., :3] / alpha[..., None], 0, 1)
Image.fromarray((np.dstack([rgb, out[..., 3]]) * 255).astype(np.uint8)).save('mark-glass.png')


# leopard-like fur (the Leopard look): golden ground, broken rosettes with tawny centres, small spots
def jitter(cell, seed):
    g = np.random.default_rng(seed)
    gy, gx = H // cell + 3, W // cell + 3
    return (
        g,
        gy,
        gx,
        (np.arange(gx)[None, :] + g.uniform(0.15, 0.85, (gy, gx))) * cell - cell,
        (np.arange(gy)[:, None] + g.uniform(0.15, 0.85, (gy, gx))) * cell - cell,
    )


def nearest(cell, cx, cy, xw, yw, *params):
    """Per pixel: offsets to the nearest jittered centre among the 3x3 neighbouring cells, and that centre's parameters."""
    j0 = np.floor((xw + cell) / cell).astype(int)
    i0 = np.floor((yw + cell) / cell).astype(int)
    bd = np.full(xw.shape, 1e9, np.float32)
    bx = np.zeros_like(bd)
    by = np.zeros_like(bd)
    bp = [np.zeros_like(bd) for _ in params]
    for di in (-1, 0, 1):
        for dj in (-1, 0, 1):
            i = np.clip(i0 + di, 0, cx.shape[0] - 1)
            j = np.clip(j0 + dj, 0, cx.shape[1] - 1)
            dx = xw - cx[i, j]
            dy = yw - cy[i, j]
            d = np.hypot(dx, dy)
            m = d < bd
            bd = np.where(m, d, bd)
            bx = np.where(m, dx, bx)
            by = np.where(m, dy, by)
            bp = [np.where(m, p[i, j], q) for p, q in zip(params, bp)]
    return bd, bx, by, bp


xw = x + (noise(120, 31) * 2 - 1) * 24 + (noise(30, 36) * 2 - 1) * 5
yw = y + (noise(120, 32) * 2 - 1) * 24 + (noise(30, 37) * 2 - 1) * 5
g, gy, gx, cx, cy = jitter(70, 21)
R = g.uniform(17, 25, (gy, gx)).astype(np.float32)
rot = g.uniform(0, 6.3, (gy, gx)).astype(np.float32)
lobes = g.integers(3, 6, (gy, gx)).astype(np.float32)
d, dx, dy, (r, ro, lo) = nearest(70, cx, cy, xw, yw, R, rot, lobes)
width = 4.2 + 2.6 * noise(40, 33)
ring = np.clip((width - np.abs(d - r)) / 1.4, 0, 1) * np.clip(
    (np.cos(lo * (np.arctan2(dy, dx) + ro)) + 0.80) * 2.0, 0, 1
)
centre = np.clip((r * 0.78 - d) / 3, 0, 1)
g2, gy2, gx2, sx, sy = jitter(58, 22)
sr = g2.uniform(3, 6.5, (gy2, gx2)).astype(np.float32)
d2, _, _, (rr,) = nearest(58, sx, sy, xw, yw, sr)
spot = np.clip((rr - d2) / 1.2, 0, 1) * (1 - np.clip((r * 1.45 - d) / 3, 0, 1))
gold = np.array([0.92, 0.65, 0.27])[:, None, None] * (0.80 + 0.30 * noise(200, 34)) * (0.82 + 0.26 * fib + 0.05 * fine)
cream = np.clip((noise(170, 35) - 0.68) * 2.4, 0, 1)[None] * 0.40
col = gold * (1 - cream) + np.array([0.98, 0.90, 0.74])[:, None, None] * cream
col = col * (1 - centre[None] * 0.45) + np.array([0.70, 0.40, 0.12])[:, None, None] * centre[None] * 0.45
ink = np.maximum(ring, spot)[None]
col = col * (1 - ink) + dark * ink
Image.fromarray((np.clip(col.transpose(1, 2, 0), 0, 1) * 255).astype(np.uint8)).filter(
    ImageFilter.GaussianBlur(0.5)
).save('launcher-leopard.jpg', quality=86)
tiles()
