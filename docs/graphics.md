# Graphics

Optics models an ATI Radeon 9700 (R300) for Tiger's own ATI driver and renders with Metal
([metal.md](metal.md) describes how). This page lists what it covers, what it does not cover yet, and how it is
checked.

## Covered

- Display: the boot display; Ptolemy's display driver with modes of 640 × 480, 800 × 600, 1024 × 768 and the window's
  size, at 8 bits (colour table), 16 bits and 32 bits; hardware cursor; gamma tables (fades and brightness).
- 2D: blits and solid fills, including the multi-rectangle blits the window server uses.
- Vertex input: arrays and immediate vertices in the R300 stream formats, index buffers, and primitives written vertex
  by vertex into the vertex unit's registers (how Tiger's driver sends polygon modes).
- Vertex programs (PVS), including relative addressing and the EXP, LOG, POW, SIN and COS operations.
- Fragment programs (US), translated to Metal shaders; up to 8 varyings.
- Primitives: triangles, strips, fans, quads, quad strips and polygons; lines of any width with stipple; points of
  any size, also per vertex and indexed, and point sprites; flat and smooth shading; polygon stipple; polygon modes
  (`glPolygonMode`: wireframe and points, also a different mode per face).
- Fixed-function stages the driver programs in hardware: fog (all three GL modes), user clip planes, logic ops,
  alpha test, colour key on textures.
- Textures: the common 8-, 16- and 32-bit formats, DXT1/3/5, YUV 4:2:2, 16- and 32-bit float, cube maps, 3D
  textures, mip levels (also of 3D and float textures), anisotropic filtering, LOD bias and border colours. Textures
  in AGP memory are recognised by content.
- Multisampling (FSAA in games, multisampled OpenGL contexts): 2 and 4 samples, resolved on the GPU with the gamma and
  alpha settings the driver asks for.
- Render targets in ARGB8888, ARGB16161616 and the narrow formats (1555, 565, 4444, I8, UV88); in VRAM and in guest
  RAM (OpenGL readback, `glReadPixels`).
- Depth and stencil, also read by the CPU (`glReadPixels` of depth, lens flares): a CPU read of a depth buffer
  exports the rows it reads first.
- Blending with constant colours, face culling, polygon offset, scissor.
- Occlusion queries (Halo), fences and the command processor's scratch registers.

## Not covered yet

| Feature | Effect |
|---|---|
| Edge flags (`glEdgeFlag`) in polygon modes | Every edge is drawn |
| The R300's 3- and 6-sample modes and its sample positions | Drawn with 4 samples; Metal's own sample positions where the R300's cannot be set exactly |
| CPU reads of a multisampled depth buffer | Not exported; the CPU sees the bytes in video memory |
| A fragment program that samples a disabled texture unit | The draw is refused (seen while a game switches to full screen) |

Signed texture components (`TX_FORMAT1` SIGNED bits) are read as unsigned. Tiger's OpenGL on this card offers no
signed texture format: no extension in its list provides one (`gl-features` prints the list), so a program cannot
make such a texture; `/Ptolemy/optics status` counts the draws that use one, in case the driver itself ever does.

Known approximations, described where the code makes them (`src/optics/draw.h`): the shape of wide line ends, the
colour key compared after filtering, a tiled depth buffer exported as the rows the CPU sees through a de-tiling
surface, a multisampled colour buffer read by the CPU as its resolved pixels. `/Ptolemy/optics status` in Tiger (with
Ptolemy Tools) counts the draws that use an approximation.

## How it is checked

`guest/tests/gl-features.c` runs inside Tiger. It draws small scenes twice, with Tiger's ATI driver on the Radeon
model and with Apple's software renderer in the same Tiger, reads both back and compares them case by case: readback
of colour and depth, geometry, fog, clip planes, lines, points, shading, polygon modes and stipple, logic ops, LOD
bias, border colours, 3D, float, mipmapped and copied textures. All 44 cases match. `gl-features --msaa` draws into
full-screen contexts with 2, 4 and 6 samples (the software renderer has none to compare with) and checks that the
edges come out antialiased.

Games checked on a MacBook Air with the M4: Halo's time demo (87–95 fps in two runs at 800 × 600 with 2× FSAA and lens
flares, which read the depth buffer every frame; 96.5 fps with OPTICS_MSAA=0, which draws one sample), Doom 3's
`timedemo demo1` (55 fps at 640 × 480), Half-Life, SimCity 4 and Fallout 2 (see the README).

## Classic (Mac OS 9 inside Tiger)

Classic has no hardware 3D on real Macs either, so only 2D games run there; they switch the display to
640 × 480 × 256 through Tiger. Classic itself does not start on Ptolemy yet.
