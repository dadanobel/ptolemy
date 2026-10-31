# Graphics

Optics models an ATI Radeon 9700 (R300) for Tiger's own ATI driver and renders with Metal. This page lists what it
covers, what it does not, and which classic games need the missing parts.

## Covered

- Display: the boot display; Ptolemy's display driver with modes of 640 × 480, 800 × 600, 1024 × 768 and the window's
  size, at 8 bits (colour table), 16 bits and 32 bits; hardware cursor; gamma tables (fades and brightness).
- 2D: blits and solid fills, including the multi-rectangle blits the window server uses.
- Vertex input: arrays and immediate vertices in the R300 stream formats, index buffers.
- Vertex programs (PVS), including relative addressing and the EXP, LOG, POW, SIN and COS operations.
- Fragment programs (US), translated to Metal shaders; up to 8 varyings.
- Triangles, strips, fans, quads and polygons; lines (one pixel); point sprites with one size per draw.
- Textures: the common 8-, 16- and 32-bit formats, DXT1/3/5, YUV 4:2:2, cube maps, mip levels and anisotropic
  filtering. Textures in AGP memory are recognised by content.
- Render targets in ARGB8888, ARGB16161616 and the narrow formats (1555, 565, 4444, I8, UV88). Multisampled targets
  are rendered with one sample and resolved as a copy, so antialiasing settings run but do not smooth edges.
- Blending with constant colours, depth and stencil, face culling, polygon offset, scissor, alpha test.
- Occlusion queries (Halo), fences and the command processor's scratch registers.

## Not modelled

| Feature | Effect |
|---|---|
| The fog unit (FG) | If Tiger's ATI driver implements glFog with it (not yet checked): no fog in Return to Castle Wolfenstein, Enemy Territory, Jedi Academy, the Pangea games (Otto Matic, Bugdom 2, Nanosaur 2, Cro-Mag Rally, Billy Frontier) and Aleph One |
| User clip planes (VAP_CLIP_CNTL) | If the driver uses them for glClipPlane (not yet checked): portals and mirrors in Quake III engine games; Aleph One clips sprites with them every frame |
| CPU reads of depth | Lens flares that read one depth pixel per frame (Jedi Knight II, Jedi Academy, Bugdom 2, Billy Frontier) |
| OpenGL frame readback | Returns black: in-game screenshots, Doom 3 save thumbnails, Jedi Academy transitions |
| Line width and stipple | Wide lines are one pixel (Otto Matic's vapour trails) |
| Per-vertex point size, indexed points | Point-sprite particles drawn at one size (Jedi Knight II snow) |
| Logic ops, polygon stipple | Aleph One's classic static effect |
| Wireframe and flat shading | Debug views only, in the games checked |
| LOD bias, texture border colour other than black, signed, float and 3D textures | No game checked depends on them in normal play |

The effects are expected from the games' published source code and documentation; most of these games have not
been run on Ptolemy yet.

## What classic games need

From the source code of id Software's releases (Quake III, Return to Castle Wolfenstein, Enemy Territory, Doom 3),
Raven's Jedi Knight II and Jedi Academy, Bungie's Marathon 2, Aleph One, the Pangea games and SDL 1.2, in order of how
many games depend on each:

1. **Gamma tables and fades** (CGSetDisplayTransferByTable, DrawSprocket and SDL fades): almost every game. Done.
2. **Display modes, including 256 colours**: StarCraft before 1.15.3, Diablo II's software mode, Marathon and most
   Mac OS 9 era 2D games. Done.
3. **Fog**: see above.
4. **Clip planes**: see above.
5. **Readback**: one-pixel depth reads and full-frame reads (above), and copies to textures (Doom 3's heat haze,
   Jedi Academy's distortion).
6. **Antialiasing requests**: games ask for multisampled pixel formats; they run, with one sample.

## Classic (Mac OS 9 inside Tiger)

Classic has no hardware 3D on real Macs either, so only 2D games run there; they switch the display to
640 × 480 × 256 through Tiger. Classic itself does not start on Ptolemy yet.
