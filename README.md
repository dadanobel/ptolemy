# Ptolemy

Ptolemy is a PowerPC Mac emulator for Apple silicon. It runs the complete PowerPC edition of Mac OS X Tiger
10.4.11 (its own kernel, Finder, WindowServer, drivers and applications) in a window or full screen on a current
Mac, with 3D graphics on the Mac's GPU.

**1.0 beta 1.** A hobby project, made for fun. Expect rough edges; most applications and games have never been tried.

## How it differs from other emulators

The usual way to run PowerPC Mac OS X on a Mac today is QEMU, directly or through UTM. SheepShaver, the other
well-known one, runs Mac OS 9.0.4 and earlier, not Mac OS X. Ptolemy differs from QEMU in how it runs the processor
and the graphics card:

- **Processor.** QEMU translates PowerPC code with its portable code generator (TCG) in an ordinary process, and every
  guest memory access looks its address up in a software TLB. Epicycle translates PowerPC code straight to ARM64 and
  runs it in a Hypervisor.framework vCPU, whose MMU walks page tables that mirror the PowerPC's: a guest load or store
  is an ARM64 load or store and a byte swap.
- **Graphics.** QEMU's Mac board has a plain framebuffer, so Tiger runs without Quartz Extreme and OpenGL falls back to
  Apple's software renderer on the emulated processor. Ptolemy models an ATI Radeon 9700: Tiger loads its own ATI
  driver, and the card's 2D and 3D work runs on the Mac's GPU through Metal.

On a MacBook Air with the M4, Tiger in Ptolemy scores about 4400 in Geekbench 2.2 (a Power Mac G5 at 1.6 GHz scores 1000),
runs Halo's built-in time demo at 85.6 fps and reaches its desktop about 10 seconds after the machine starts.
Published Geekbench 2.2 results for Tiger in [QEMU 7.0 on an M1 Mac mini](https://browser.geekbench.com/geekbench2/2730728)
and [UTM 3.2.4 on an M1 Pro MacBook Pro](https://browser.geekbench.com/geekbench2/2730718) are 731 and 774. Those Macs
are older, so the comparison is approximate.

QEMU runs on almost any computer and starts many more systems: Mac OS 9, other versions of Mac OS X, Linux. Ptolemy
needs a Mac with Apple silicon and so far runs Tiger only.

## Components

| Name | What it is | Where |
|---|---|---|
| **Epicycle** | PowerPC G3/G4 to ARM64 JIT that runs inside Hypervisor.framework; AltiVec on NEON | `epicycle/` |
| **Optics** | ATI Radeon 9700 (R300) model: command processor, 2D engine, vertex and fragment programs translated to Metal. Tiger's own ATI driver drives it, so Quartz Extreme, Core Image and OpenGL games run on the Mac's GPU | `src/optics/` |
| **Harmonics** | Screamer sound and its DBDMA channels, played through Core Audio | `src/audio.c`, `src/audio_macos.c` |
| Machine | PowerBook5,8: G4 7447A 1.67 GHz, 2 GB, MacIO, PMU, ATA disk, ATAPI CD/DVD, USB keyboard, tablet, mouse and storage, RTL8139 Ethernet with user-mode networking, NVRAM | `src/`, `include/` |
| Firmware | A fork of [OpenBIOS](https://github.com/openbios/openbios) (Open Firmware), built reproducibly | `firmware/openbios/` |
| Ptolemy Tools | Runs inside Tiger: display driver (modes, hardware cursor, gamma), clipboard, file transfer, orderly restart and shut down | `guest/` |
| Ptolemy.app | Launcher: installs Tiger from your own DVD image, keeps several machines, sets disk size, processor and memory | `product/` |

The firmware is the only third-party code in the machine; everything else was written for Ptolemy.
[THIRD-PARTY.md](THIRD-PARTY.md) lists every outside component, including test-only ones.

## What has been tried

On a 13.6-inch MacBook Air with the M4 (10-core CPU and GPU, 24 GB), macOS 27, with Tiger 10.4.11 on the G4 7447A:

| Software | Result |
|---|---|
| Mac OS X Tiger 10.4 | Installs from the retail DVD image; updates to 10.4.11 with the Combo update; Finder and Dock with Quartz Extreme |
| Halo (Universal) | Campaign plays. Built-in time demo at 800 × 600 with 2× FSAA: 85.6 fps |
| Doom 3 1.3.1 | Renders completely (ARB2 path). `timedemo demo1` at 640 × 480: 53 fps |
| Half-Life (Xash3D) | 1024 × 768: 430–650 fps, depending on the run |
| SimCity 4 | Starts from its disc image; the tutorial city renders |
| Fallout 2 | 640 × 480 in 256 colours |
| Cinebench R10 | CPU 2773, OpenGL 5665 |
| Geekbench 2.2 | 4381–4447 |
| Xbench 1.3 | Runs |

Everything else is untested. Reports are welcome.

## Known limitations

- **Graphics.** Not modelled yet: fixed-function fog, user clip planes, wide and stippled lines (lines are one pixel),
  wireframe polygons, flat shading, logic ops, per-vertex point size, LOD bias, texture border colours,
  signed, float and 3D textures. The CPU sees stale depth values, and OpenGL frame readback returns black. Games that
  rely on these may look wrong: see [docs/graphics.md](docs/graphics.md) for which ones.
- **Sleep.** When Tiger goes to sleep, the machine stops. Set Energy Saver to never put the computer to sleep.
- **NVRAM.** Settings that Mac OS X keeps in NVRAM (for example Startup Disk) are not saved.
- **Classic.** The Mac OS 9 environment does not start, and the firmware cannot start a disk that Tiger marked for
  Mac OS 9.
- **Files.** Files dropped onto the window arrive without resource forks, type and creator.
- **Not available:** more than one CPU, CD audio, game controllers, sound input, incoming network connections.
- **Mouse in games.** A captured mouse can feel fast; lower the game's own mouse sensitivity.

## Plans

Dual G4, Power Mac G5 and Mac OS X Leopard are planned, without dates. The next graphics work is fog and clip planes.

## Requirements

- A Mac with Apple silicon and macOS 26 or later.
- Your own Mac OS X Tiger 10.4 PowerPC install DVD image. Ptolemy does not include or download Apple software.

There is no prebuilt download yet: Ptolemy is built from source.

## Building

You need the Xcode command line tools and Python 3.

```sh
sh build.sh --test        # the engine (build/ptolemy-engine) and its tests
```

The firmware ROM, Ptolemy Tools and the app have their own steps: see [docs/building.md](docs/building.md).

## Documentation

- [docs/architecture.md](docs/architecture.md): how the pieces fit together
- [docs/building.md](docs/building.md): building the engine, the ROM, Ptolemy Tools and Ptolemy.app
- [docs/graphics.md](docs/graphics.md): what the Radeon model covers, and what classic games need
- [epicycle/README.md](epicycle/README.md): the CPU engine and its tests

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Mac OS X, Halo, Doom 3 and the other titles above belong to their owners;
Ptolemy is not affiliated with Apple or with any of them.
