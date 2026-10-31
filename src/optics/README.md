# Optics

Ptolemy's ATI Radeon 9700 (R300) model. [docs/architecture.md](../../docs/architecture.md) explains how it fits into
the machine and [docs/graphics.md](../../docs/graphics.md) lists what it covers.

| File | Contents |
|---|---|
| `frontend.c`, `frontend.h` | The device: registers, PCI and AGP windows, the command processor (ring, indirect buffers, PM4 packets), 2D engine, completion and occlusion queries, scanout, CPU access to VRAM |
| `ring.c`, `command_stream.c`, `r300_state.c` | Ring reader, packet stream, the 3D register shadow |
| `draw.c`, `draw.h` | One R300 draw (vertex fetch, vertex and fragment programs, rasterizer, textures, targets) decoded into a description |
| `pixel.c` | Texel and pixel formats, with a CPU reference decoder |
| `executor.h`, `frontend_metal.mm` | The Metal executor: surfaces, depth storage, texture caches, programs, batches |
| `renderer.mm`, `renderer.h` | The Metal renderer underneath: targets, shaders from R300 programs, samplers, command buffers |
| `io.c`, `guard.c`, `timeline.c`, `address.c` | Ownership of VRAM pages between CPU and GPU, completion ledger, GPU addresses |
| `cpu_access.c`, `cpu_guard.c`, `hvf_access.c` | The CPU's direct view of VRAM and its fault route |
| `foundation/` | PM4 parsing, R300 shader decoding and evaluation, Metal shader generation, MSAA and resolve helpers |
| `diagnostics.c`, `guest/agent_builtin.h` | Status for Ptolemy Agent (`/Ptolemy/optics status` in Tiger) |

Diagnostic switches are environment variables named `OPTICS_*`; the code describes each where it is read. The
`PtR2` prefix marks the renderer layer.
