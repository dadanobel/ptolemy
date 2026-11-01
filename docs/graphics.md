# Graphics

Optics models an ATI Radeon 9700 (R300) for Tiger's own ATI driver and renders with Metal
([metal.md](metal.md) describes how). This page lists what it covers, what it does not cover yet, and how it is
checked.

## Covered

- Display: the boot display; Ptolemy's display driver with modes of 640 × 480, 800 × 600, 1024 × 768 and the window's
  size, at 8 bits (colour table), 16 bits and 32 bits; hardware cursor; gamma tables (fades and brightness).
- 2D: blits and solid fills, including the multi-rectangle blits the window server uses.
- Vertex input: arrays and immediate vertices in the R300 stream formats, index buffers.
- Vertex programs (PVS), including relative addressing and the EXP, LOG, POW, SIN and COS operations.
- Fragment programs (US), translated to Metal shaders; up to 8 varyings.
- Primitives: triangles, strips, fans, quads, quad strips and polygons; lines of any width with stipple; points of
  any size, also per vertex, and point sprites; flat and smooth shading; polygon stipple.
- Fixed-function stages the driver programs in hardware: fog (all three GL modes), user clip planes, logic ops,
  alpha test, colour key on textures.
- Textures: the common 8-, 16- and 32-bit formats, DXT1/3/5, YUV 4:2:2, 16- and 32-bit float, cube maps, 3D
  textures, mip levels, anisotropic filtering, LOD bias and border colours. Textures in AGP memory are recognised by
  content.
- Render targets in ARGB8888, ARGB16161616 and the narrow formats (1555, 565, 4444, I8, UV88); in VRAM and in guest
  RAM (OpenGL readback, `glReadPixels`).
- Depth and stencil, also read by the CPU (`glReadPixels` of depth, lens flares): a CPU read of a depth buffer
  exports the rows it reads first.
- Blending with constant colours, face culling, polygon offset, scissor.
- Occlusion queries (Halo), fences and the command processor's scratch registers.

## Not covered yet

| Feature | Effect |
|---|---|
| Wireframe and point polygon modes (`glPolygonMode`) | Draw nothing: Tiger's ATI driver sends no GPU work for them, and where its fallback draws is not known yet. Debug views and some 3D editors |
| Multisampling | Antialiasing settings run with one sample, so edges are not smoothed |
| Signed texture components | Read as unsigned (bump maps that store signed offsets) |
| Mip levels of 3D and float textures | Level 0 is used |
| Indexed point sprites | Refused (not seen in a game yet) |

Known approximations, described where the code makes them (`src/optics/draw.h`): the shape of wide line ends, the
colour key compared after filtering, a tiled depth buffer exported as the rows the CPU sees through a de-tiling
surface. `/Ptolemy/optics status` in Tiger (with Ptolemy Tools) counts the draws that use an approximation.

## How it is checked

`guest/tests/gl-features.c` runs inside Tiger. It draws small scenes twice, with Tiger's ATI driver on the Radeon
model and with Apple's software renderer in the same Tiger, reads both back and compares them case by case: readback
of colour and depth, geometry, fog, clip planes, lines, points, shading, polygon stipple, logic ops, LOD bias,
border colours, 3D, float and copied textures. 44 of its 47 cases match; the three that do not are the polygon
modes above.

Games checked on a MacBook Air with the M4: Halo's time demo (82.5 fps at 800 × 600 with lens flares, which read the
depth buffer every frame; the game's FSAA setting draws with one sample), Doom 3's `timedemo demo1` (55 fps at 640 × 480), Half-Life, SimCity 4 and
Fallout 2 (see the README).

## Classic (Mac OS 9 inside Tiger)

Classic has no hardware 3D on real Macs either, so only 2D games run there; they switch the display to
640 × 480 × 256 through Tiger. Classic itself does not start on Ptolemy yet.
