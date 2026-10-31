# Architecture

One `ptolemy-engine` process runs one machine. Ptolemy.app starts it with the machine's disk, ROM and settings, and
the engine opens its own window.

## The machine

A PowerBook5,8 (2005 PowerBook G4): G4 7447A at 1.67 GHz on a 167 MHz bus, 512 KiB L2, 2 GB of memory and a
25 MHz time base. A G3 750 and a G4 7400 can be chosen instead (`--cpu`). Tiger sees this machine in System
Profiler and `sysctl`.

| Part | Model | Source |
|---|---|---|
| CPU | Epicycle (below) | `epicycle/` |
| PCI/AGP host | UniNorth: configuration space, BAR assignment, AGP GART | `src/board.c`, `src/firmware.c` |
| MacIO | KeyLargo: OpenPIC interrupt controller, VIA and PMU (power, clock, battery), DBDMA, Screamer sound | `src/platform.c`, `src/pmu.c`, `src/dma.c`, `src/audio.c` |
| Storage | ATA disk, ATAPI CD/DVD-RW combo drive (SCSI/MMC commands), USB mass storage | `src/ata.c`, `src/atapi.c`, `src/scsi.c`, `src/usb_msc.c` |
| USB | OHCI with HID keyboard, absolute tablet and relative mouse | `src/ohci.c`, `src/hid.c`, `src/input.c` |
| Network | RTL8139 with user-mode NAT (no privileges) or vmnet | `src/net_rtl.c`, `src/net_user.c`, `src/net_macos.m` |
| NVRAM | Open Firmware and Mac OS partitions, saved next to the disk | `src/nvram.c` |
| Graphics | Optics (below) | `src/optics/` |

Disks are sparse bundles, so copies are APFS clones that cost almost no space. `--overlay` keeps the guest's writes
in memory instead (tests). CD/DVD and disk images can be inserted from the window's Media menu.

## Epicycle: the CPU

Epicycle translates PowerPC code to ARM64 one block at a time and runs it inside a Hypervisor.framework vCPU, with
the guest's memory mapped into the vCPU's stage-2 address space. The PowerPC MMU (BATs, segment registers, hashed
page table) is modelled in `mmu.c`; translated code checks its assumptions and exits to the interpreter for rare
instructions and corner cases. Floating point follows PowerPC rounding and FPSCR rules; AltiVec runs on NEON.
Device accesses leave the vCPU as stage-2 faults or hypercalls and are handled by the engine's thread.
See [`epicycle/README.md`](../epicycle/README.md).

## Optics: the graphics card

Optics models an ATI Radeon 9700 (R300) closely enough for Tiger's own ATI driver. The driver writes PM4 command
packets into a ring; Optics' command processor runs them on its own thread:

- **2D**: blits and fills run directly on VRAM.
- **3D**: each draw's register state (vertex fetch, vertex program, rasterizer, fragment program, blending, depth
  and stencil) is decoded into a description (`draw.c`) that the Metal executor (`frontend_metal.mm`,
  `renderer.mm`) renders. R300 vertex and fragment programs become Metal shaders; textures are decoded once and
  cached (in AGP memory by content, since the CPU writes there unseen).
- **Memory**: render targets stay on the GPU and are written back to VRAM only when the CPU, the 2D engine or the
  display reads them. The CPU maps VRAM directly, in its own byte order.
- **Completion**: fences and occlusion queries are answered in the order the driver expects.
- **Display**: the scanout is handed to the window as finished frames.

What is not modelled is listed in [graphics.md](graphics.md).

## Firmware and boot

The ROM is Ptolemy's fork of OpenBIOS (`firmware/`). The engine passes the board description through a
configuration port and sets the Open Firmware `boot-command`, which installs Ptolemy's display driver (an NDRV
built from `guest/ndrv/`) in the screen node and boots Tiger's BootX from the disk.

The display driver gives Tiger display modes up to the window's size at 8, 16 and 32 bits, a colour table, a
64 × 64 hardware cursor and gamma; it reaches the engine through a private instruction.

## Inside Tiger: Ptolemy Tools

Ptolemy Agent (`guest/agent/`) runs in Tiger's menu bar and talks to the engine through an authenticated hypercall
channel. It carries the clipboard, file transfers in both directions, the window's Restart and Shut Down (Tiger
shuts down as if from its Apple menu) and diagnostics. Ptolemy.app installs it into every new machine.

## The window

The window (`src/stand_window_macos.m`, `src/input_macos.m`) presents frames with Metal at 60 Hz, scales them to the
window or full screen, draws the guest's hardware cursor and applies the guest's gamma. The mouse is an absolute
tablet unless a game captures it. A web gateway (`src/web.c`, `src/web_macos.m`) fetches http:// pages over HTTPS on behalf of
Tiger's browsers.
