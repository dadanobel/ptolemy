# Graphics

Optics models an ATI Radeon 9700 (R300) for Mac OS X's own ATI driver (Tiger and Leopard) and renders with Metal
([metal.md](metal.md) describes how). This page lists what it covers, what it does not cover yet, and how it is
checked.

## Covered

- Display: the boot display; Ptolemy's display driver with modes of 640 × 480, 800 × 600, 1024 × 768 and the window's
  size, at 8 bits (colour table), 16 bits and 32 bits; hardware cursor; gamma tables (fades and brightness).
- 2D: blits and solid fills, including the multi-rectangle blits the window server uses.
- Vertex input: arrays and immediate vertices in the R300 stream formats, index buffers, and primitives written vertex
  by vertex into the vertex unit's registers (how Tiger's driver sends polygon modes).
- Vertex programs (PVS), including relative addressing and the EXP, LOG, POW, SIN and COS operations.
- Fragment programs (US), translated to Metal shaders; up to 8 varyings. A shader not compiled yet compiles in the
  background: the draw that needs it waits in the command processor while Tiger's CPU, sound and input go on.
- Primitives: triangles, strips, fans, quads, quad strips and polygons; lines of any width with stipple; points of
  any size, also per vertex and indexed, and point sprites; flat and smooth shading; polygon stipple; polygon modes
  (`glPolygonMode`: wireframe and points, also a different mode per face, with edge flags).
- Fixed-function stages the driver programs in hardware: fog (all three GL modes), user clip planes, logic ops,
  alpha test, colour key on textures.
- Textures: the packed formats (332, 4444, 1555, 565, 8888, 2101010), luminance, alpha, intensity, 16 bits per
  channel, DXT1/3/5 (a DXT1 texel the block marks transparent keeps alpha 0 where Tiger's driver selects ONE for
  alpha: SimCity 4's trees and buildings need it), YUV 4:2:2 (also Apple's client storage and rectangle textures),
  16-bit and 32-bit float, cube maps, 3D textures, mip levels down to 1 × 1 as Tiger's driver stores them (also of 3D
  and float textures), anisotropic filtering, LOD bias and border colours. Textures in AGP memory are recognised by
  content. A fragment program that samples a disabled texture unit reads (0, 0, 0, 1), as `ARB_fragment_program`
  specifies.
- Multisampling (FSAA in games, multisampled OpenGL contexts): 2 and 4 samples, resolved on the GPU with the gamma and
  alpha settings the driver asks for.
- Render targets in ARGB8888, ARGB16161616 and the narrow formats (1555, 565, 4444, I8, UV88); in VRAM and in guest
  RAM (OpenGL readback, `glReadPixels`).
- Depth and stencil, also read by the CPU (`glReadPixels` of depth, lens flares): a CPU read of a depth buffer
  exports the rows it reads first.
- Blending with constant colours, and the blender's discard of unchanged pixels (only while blending, as on the R300:
  Halo's shadow blur draws with it set and blending off); face culling, polygon offset, scissor.
- Occlusion queries (Halo), fences and the command processor's scratch registers.

## Not covered yet

| Feature | Effect |
|---|---|
| The R300's 3- and 6-sample modes and its sample positions | Drawn with 4 samples; Metal's own sample positions where the R300's cannot be set exactly |
| CPU reads of a multisampled depth buffer | Exported only for 2 samples (the tiled D24S8 layout Leopard's driver reads, as in Chess); other sample counts are not exported and the CPU sees the bytes in video memory |

Signed texture components (`TX_FORMAT1` SIGNED bits) are read as unsigned. Tiger's OpenGL on this card offers no
signed texture format: no extension in its list provides one (`gl-features` prints the list), so a program cannot
make such a texture; `/Ptolemy/optics status` counts the draws that use one, in case the driver itself ever does.

Known approximations, described where the code makes them (`src/optics/draw.h`): the shape of wide line ends, the
colour key compared after filtering, a tiled depth buffer exported as the rows the CPU sees through a de-tiling
surface, a multisampled colour buffer read by the CPU as its resolved pixels, 10-bit texture channels sampled with 8
bits, a tiled YUV texture read as linear. `/Ptolemy/optics status` in Tiger (with Ptolemy Tools) counts the draws that
use an approximation.

## How it is checked

`guest/tests/gl-features.c` runs inside Tiger. It draws small scenes twice, with Tiger's ATI driver on the Radeon
model and with Apple's software renderer in the same Tiger, reads both back and compares them case by case: readback
of colour and depth, geometry, fog, clip planes, lines, points, shading, polygon modes and stipple, logic ops, LOD
bias, border colours, 3D, float, mipmapped and copied textures, every texture format above, every mip level of 8-,
16-, 32-bit and DXT textures, and edge flags. 101 of the 105 cases match; the 4 DXT format cases are skipped, because
the software renderer has no S3TC to compare with (the DXT mip cases give it the same colours uncompressed). Tiger's
own conversion of RGBA bytes to RGB5_A1 (libGLImage, two texels at a time) carries the first texel's alpha bit into
the second, on a real Mac too; the software renderer does not show it, so the two cases that use it model it in the
reference image. `gl-features --msaa` draws into full-screen contexts with 2, 4 and 6 samples (the software renderer
has none to compare with) and checks that the edges come out antialiased.

Every session log ends with the frame pacing (frames while streaming, gaps by length, the longest), the device
accesses that waited for the command processor, and the shader compiles that took 50 ms or more.

Games checked on a MacBook Air with the M4: Halo's time demo (87–95 fps in two runs at 800 × 600 with 2× FSAA and lens
flares, which read the depth buffer every frame; 96.5 fps with OPTICS_MSAA=0, which draws one sample), Doom 3's
`timedemo demo1` (55 fps at 640 × 480), Half-Life, SimCity 4 and Fallout 2 (see the README).

## Classic (Mac OS 9 inside Tiger)

Classic has no hardware 3D on real Macs either, so only 2D games run there; they switch the display through Tiger.
Classic starts from a Mac OS 9.2.2 System Folder (the first start takes about two minutes) and runs Age of Empires.
