<p align="center"><img src="assets/github-social-1280x640.jpg" alt="Ptolemy" width="640"></p>

# Ptolemy

Ptolemy is a PowerPC Mac emulator for Apple silicon. It runs Mac OS X Tiger 10.4.11 and Leopard 10.5 for PowerPC,
unmodified (their own kernel, drivers, Finder and applications), in a window or full screen on a current Mac. The
PowerPC code is translated to ARM64 and runs on the Mac's CPU cores under Hypervisor.framework; Mac OS X's own ATI
driver drives an emulated Radeon 9700 whose 2D and 3D work runs on the Mac's GPU through Metal.

**1.0 beta 4.** A hobby project, made for fun by a human and several frontier models. Expect rough edges: most applications and games have never been tried.
[Download it](#download) · [what's new](#whats-new-in-beta-4) · [how it works](#how-it-works) · [build it](#building)

<p align="center"><img src="assets/screenshots/ptolemy-window.png" alt="Ptolemy's window with a Tiger and a Leopard machine" width="720"></p>

## In short

If you miss your PowerBook G4 or the Power Mac under the desk, Ptolemy gives you one back, in a window on your Mac.

- **Your own Mac OS X.** Make an image of your Tiger or Leopard install DVD, drop it onto Ptolemy's window, choose the
  disk size, and the installer runs as it did in 2005. Ptolemy does not include or download Apple software.
- **Faster than the fastest real G4, mostly.** In Geekbench 2 one emulated G4 scores about 5000 and two of them about
  6000; the fastest real G4, a Power Mac G4 with two 1.42 GHz processors, averages about 1220. Some floating-point
  library code is still slower than on a real G4 (see [Known limitations](#known-limitations)).
- **3D on the Mac's GPU.** Mac OS X's own ATI driver believes it talks to a Radeon 9700, so Quartz Extreme, Core Image
  and OpenGL games run on the Mac's GPU: Halo, Doom 3, Half-Life (Xash3D) and SimCity 4 among those we tried. Classic
  runs inside Tiger with a Mac OS 9.2.2 System Folder you bring (Age of Empires plays).
- **Two G4s in Leopard.** Choose *PowerPC Dual G4 7447A* in a Leopard machine's settings.
- **At home on today's Mac.** The clipboard both ways, files dropped onto the window, disk images inserted as USB
  disks, sound, the network and the Mac's camera in Photo Booth. A web gateway lets the systems' own Safari open
  today's HTTPS sites.
- **Several machines.** Each has its own disk (up to 120 GB), processor (G3 750, G4 7400, G4 7447A, Dual G4 for
  Leopard) and memory (512 MB to 2 GB). Disks are sparse bundles: they take only the space they use.

## What's new in beta 4

- **Faster.** Against beta 3 on one G4 7447A, and beta 4 on Dual G4 in Leopard. One run each, one session after
  another on the same Mac (MacBook Air M4, window 1024 × 768):

  | | Tiger 1.0b3 | Tiger 1.0b4 | Leopard 1.0b3 | Leopard 1.0b4 | Leopard 1.0b4, Dual G4 |
  |---|---|---|---|---|---|
  | Geekbench 2 | 4539 | 5084 (+12%) | 4477 | 4943 (+10%) | 6067 |
  | Cinebench R10, 1 CPU | 2876 | 3138 (+9%) | 2821 | 3141 (+11%) | 3190 |
  | Cinebench R10, x CPU | — | — | — | — | 5461 (1.76×)¹ |
  | Cinebench R10 OpenGL | 5376² | 5225 (−3%)² | 5345 | 5696 (+7%) | 5601 |
  | Xbench vecLib FFT | 36.5 | 65.6 (×1.8) | 38.4 | 62.6 (×1.6) | 64.7 |
  | Xbench Quartz | 367.7 | 361.7 (−2%) | 452.5 | 468.7 (+4%) | 463.1 |
  | Xbench OpenGL | 166.7 | 212.4 (+27%) | 167.7 | 212.3 (+27%) | 209.7 |
  | Xbench User Interface | 181.6 | 293.6 (+62%) | 137.1 | 184.8 (+35%) | 207.7 |

  ¹ A separate run after the others. ² OpenGL alone, two runs each (beta 3: 5547, 5205; beta 4: 5408, 5041).

  Logging in and starting applications are smoother: the translator keeps twice as much code before it starts over.
- **Dual G4 for Leopard**, with Leopard's own multiprocessor kernel and scheduler. See
  [Two processors](#two-processors-dual-g4).
- **No first-use stutter in Halo.** A drawing whose shader is still being compiled for Metal waits up to 10 ms, then
  is skipped for that frame, instead of stopping it.
- **The Mac's camera** as a USB camera in Mac OS X (Photo Booth); macOS asks for permission the first time.
- **Lighter when idle.** USB frames go in batches while the bus is quiet, and the graphics card's service runs only
  while it has work.
- **Ptolemy Tools update themselves** on machines made with an earlier beta, at their next start.
- **A new window** in the style of macOS 26, with pictures of Tiger and Leopard; it names the installed system.
- **Antiochus and Rosetta Stone**, a test suite and a demo, in the Ptolemy Tools menu.

## Emulated, translated, native

Ptolemy is an emulator: Apple silicon cannot run PowerPC code, and every part of the PowerBook G4 that Tiger sees is
modelled in software. What makes it fast is where that work runs.

- **Emulated: the machine.** The board, disk, CD/DVD, USB, network, sound and the Radeon 9700 are models written for
  Ptolemy, close enough to the real hardware for Tiger's own drivers.
- **Translated: the processor.** Epicycle compiles Tiger's PowerPC code, the kernel included, to ARM64 one block at a
  time and keeps the translations; an interpreter handles rare instructions and corner cases.
- **On the Mac's hardware.** The translated code runs on the Mac's CPU cores inside a Hypervisor.framework vCPU. The
  Mac's MMU does the PowerPC's address translation through page tables that mirror Tiger's, so a guest load or store
  is an ARM64 load or store and a byte swap. The Radeon's drawing runs on the Mac's GPU: its vertex and fragment
  programs become Metal shaders.
- **Added to Tiger.** Ptolemy's display driver (modes up to the window's size, cursor, gamma), which the firmware hands
  to Tiger, and Ptolemy Agent (clipboard, file transfer, shut down), which Ptolemy.app installs with each machine.
  Both talk to Ptolemy through private calls instead of emulated hardware. Tiger's own files are not changed.

Hypervisor.framework gives the translated code a memory management unit and isolation; it does not run Tiger's code
as it is, the way a virtual machine runs a guest made for the same processor. Rosetta 2 is the nearer comparison: it
translates an Intel app's code for macOS, while Epicycle translates a whole PowerPC system, kernel and drivers
included, on emulated hardware.

## How it differs from other emulators

The usual way to run PowerPC Mac OS X on a Mac today is QEMU, directly or through UTM. SheepShaver, the other
well-known one, runs Mac OS 9.0.4 and earlier, not Mac OS X. Ptolemy differs from QEMU in how it runs the processor
and the graphics card:

- **Processor.** QEMU translates PowerPC code with its portable code generator (TCG) in an ordinary process, and every
  guest memory access looks its address up in a software TLB. Epicycle generates ARM64 code directly and leaves
  address translation to the Mac's MMU.
- **Graphics.** QEMU's Mac board has a plain framebuffer, so Tiger runs without Quartz Extreme and OpenGL falls back to
  Apple's software renderer on the emulated processor. In Ptolemy, Tiger's ATI driver accelerates Quartz Extreme,
  Core Image and OpenGL on the Mac's GPU.

On a MacBook Air with the M4, Tiger in Ptolemy scores about 5100 in Geekbench 2.2 (a Power Mac G5 at 1.6 GHz scores
1000), runs Halo's built-in time demo at about 90 fps with 2× FSAA and reaches its desktop about 10 seconds after the
machine starts.
Published Geekbench 2.2 results for Tiger in [QEMU 7.0 on an M1 Mac mini](https://browser.geekbench.com/geekbench2/2730728)
and [UTM 3.2.4 on an M1 Pro MacBook Pro](https://browser.geekbench.com/geekbench2/2730718) are 731 and 774. Those Macs
are older, so the comparison is approximate.

QEMU runs on almost any computer and starts many more systems: Mac OS 9, other versions of Mac OS X, Linux. Ptolemy
needs a Mac with Apple silicon and so far runs Tiger and Leopard only.

[ppcosxkvm](https://github.com/linuxkid473/ppcosxkvm), a QEMU fork, also emulates the Radeon 9700 for Tiger's ATI
driver, starts Jaguar and Leopard as well and runs on Linux and Intel computers too. We recommend trying it as well.

## How it works

### The machine

Ptolemy models a PowerBook5,8, the 2005 15-inch PowerBook G4: a G4 7447A at 1.67 GHz on a 167 MHz bus, 512 KiB of L2,
up to 2 GB of memory and a 25 MHz timebase. Mac OS X sees this machine in System Profiler and `sysctl`.

| Part | Model | Source |
|---|---|---|
| CPU | Epicycle: G3 750, G4 7400, G4 7447A, two 7447A | `epicycle/` |
| PCI/AGP host | UniNorth: configuration space, BAR assignment, AGP GART | `src/board.c`, `src/firmware.c` |
| MacIO | KeyLargo: OpenPIC interrupt controller, VIA and PMU (power, clock, battery), DBDMA, Screamer sound | `src/platform.c`, `src/pmu.c`, `src/dma.c`, `src/audio.c` |
| Storage | ATA disk, ATAPI CD/DVD-RW combo drive (SCSI/MMC commands), USB mass storage | `src/ata.c`, `src/atapi.c`, `src/scsi.c`, `src/usb_msc.c` |
| USB | OHCI with HID keyboard, absolute tablet and relative mouse; the Mac's camera as a UVC camera | `src/ohci.c`, `src/hid.c`, `src/input.c`, `src/usb_uvc.c` |
| Network | RTL8139 with user-mode NAT (no privileges) or vmnet | `src/net_rtl.c`, `src/net_user.c`, `src/net_macos.m` |
| NVRAM | Core99 flash (two banks) with the Open Firmware and Mac OS partitions | `src/nvram.c` |
| Graphics | Optics: ATI Radeon 9700 (R300) | `src/optics/` |
| Firmware | A fork of OpenBIOS (Open Firmware) | `firmware/openbios/` |

### Epicycle: the processor

Epicycle translates PowerPC code to ARM64 one block at a time, without an intermediate representation: superblocks, a
register cache per block, blocks linked by direct branches, a jump cache for indirect branches, compares fused with
the branches that follow them. It runs the code inside a Hypervisor.framework vCPU, with the guest's memory mapped into
the vCPU's stage-2 address space. The PowerPC MMU (BATs, segment registers, the hashed page table) is modelled in
`mmu.c`; translated code checks its assumptions and exits to the interpreter for rare instructions and corner cases.
Device accesses leave the vCPU as stage-2 faults or hypercalls and are handled on the host. Floating point follows
PowerPC rounding and FPSCR rules; AltiVec runs on NEON. [epicycle/README.md](epicycle/README.md) has the details.

### Optics: graphics on Metal

<p align="center"><img src="assets/metal-banner.jpg" alt="Optics: Radeon 9700 graphics on the Mac's GPU" width="640"></p>

Optics, the Radeon 9700 model, works at the level of the card's registers and command ring, so Mac OS X's ATI driver,
OpenGL and Quartz Extreme run as they would on the real card. Each draw the driver sends becomes one Metal draw:

- the card's vertex and fragment programs become Metal shaders, with fog, clip planes, logic ops and the other
  fixed-function stages built in; a draw whose shader is still compiling waits up to 10 ms, then is skipped for
  that frame;
- compute kernels decode textures on the GPU, DXT and YUV included;
- render targets are Metal textures that own the guest's video memory, and Hypervisor.framework's page protection
  tells Optics when the guest's CPU touches them; with FSAA they are multisampled and resolved on the GPU;
- the driver's fences and occlusion queries complete from Metal's completion handlers;
- the window presents through a `CAMetalLayer` at 60 Hz.

The engine links Apple's frameworks only: no host OpenGL, no Vulkan layer, no shader cross-compiler.
[docs/metal.md](docs/metal.md) describes this in detail; [docs/graphics.md](docs/graphics.md) lists what the model
covers and what it does not cover yet.

### Two processors (Dual G4)

<p align="center"><img src="assets/screenshots/leopard-dual-g4-cinebench.jpg" alt="Cinebench R10 in Leopard on Dual G4" width="640"></p>

A Leopard machine can have two G4 7447A, as in a dual-processor Power Mac G4. Each processor runs on its own host
thread, and guest code on both runs in parallel on two of the Mac's cores.

- **One engine lock.** A processor's thread holds it whenever it is outside guest code: translating, handling an exit,
  running a device model, walking page tables. So the translator and the devices are used by one thread at a time,
  as with one processor; the lock is first come, first served. A change the other processor must not run across
  (code being thrown away, a page leaving its tables, the timebase's coefficients) first stops it at a block boundary.
- **Leopard starts the second processor itself**, as on the real machine: it is held in reset until the kernel
  releases it through KeyLargo. The OpenPIC delivers interrupts and inter-processor interrupts to both. Both read one
  timebase; each has its own decrementer.
- **Memory order.** Apple silicon reorders memory more freely than a G4. While two processors run, the PowerPC's
  barriers become ARM barriers: `sync` is `dmb ish`, `eieio` `dmb ishst`, `isync` `dmb ishld` and `isb`, and
  `stwcx.` a compare-and-swap. Correctness tests (atomic counters, message passing, store buffering, the timebase seen
  from both processors) find zero violations, idle and under load.
- **Idle.** An idle processor naps, and its thread first polls for a short, adaptive moment before it sleeps, so a
  quick hand-over between the two does not wait for a thread to wake.

What it costs: single-threaded work runs as with one processor, multithreaded Cinebench renders 1.76–1.84× faster, idle
takes about 1 s of host CPU a minute more, OpenGL is about 5% lower. Two threads fighting over one lock are much
slower than on one processor: each hand-over crosses into the Mac. Tiger keeps one processor: its kernel wakes every
processor 100 times a second, idle or not.

The screenshot is the separate run behind the table's x CPU score: 1.76× with two processors. Its OpenGL score, 4684,
is below the session's 5601; results on the Air vary from run to run.

### Inside Mac OS X: Ptolemy Tools

Ptolemy Tools run inside Tiger and Leopard and talk to the engine through private calls instead of emulated hardware.
The display driver, which the firmware hands to Mac OS X, gives display modes up to the window's size, a hardware
cursor and gamma. Ptolemy Agent, in the menu bar, carries the clipboard and files, restarts and shuts down the system in
order and, in Leopard, keeps the system from sleeping on its own. Ptolemy.app installs the Tools
into every new machine and updates them on machines made with an earlier beta; Tools someone else put in that place
are left alone. Mac OS X's own files are not changed.

### How it is tested

- `sh build.sh --test` builds the engine and runs its tests: the JIT against the interpreter, bare-metal PowerPC
  programs for the MMU and exceptions, each device model on its own, the graphics card's threads under
  ThreadSanitizer.
- **Antiochus**, in the Ptolemy Tools menu, runs CPU and GPU cases inside Mac OS X and compares every GPU image with
  Apple's software renderer.
- Each beta is measured against the one before with Geekbench 2, Cinebench R10 and Xbench, in the same window, one
  session after another.

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

## Download

`Ptolemy-1.0b4.zip` on the [Releases](https://github.com/dadanobel/ptolemy/releases) page holds Ptolemy.app with the
engine, the firmware and Ptolemy Tools. You need:

- A Mac with Apple silicon and macOS 15 Sequoia or later (tried on macOS 15 with an M1 Max and on macOS 27 with
  an M4).
- An image of your own Mac OS X Tiger 10.4 or Leopard 10.5 PowerPC install DVD (see below). Ptolemy does not
  include or download Apple software.

Unzip it and move Ptolemy.app to Applications. Ptolemy is signed ad hoc, not notarized by Apple, so macOS refuses to
open it the first time. Open System Settings → Privacy & Security and click Open Anyway next to the message about
Ptolemy, or remove the quarantine in Terminal:

```sh
xattr -dr com.apple.quarantine /Applications/Ptolemy.app
```

Then choose your Tiger or Leopard DVD image in Ptolemy (or drop it onto the window), set the disk size and start.
The installer runs in the window; when it restarts, Ptolemy copies Ptolemy Tools onto the new disk and starts the new
system from it. To update Tiger to 10.4.11, drop Apple's Mac OS X 10.4.11 Combo Update (PPC) onto the window: it arrives on
Tiger's desktop.

### An image of your install DVD

You need the disc and a DVD drive, such as Apple's USB SuperDrive.

1. Insert the DVD and open Disk Utility (Applications → Utilities).
2. Select the disc in the sidebar and choose File → New Image → Image from the disc (for example "Mac OS X Install DVD").
3. Choose the format DVD/CD master, no encryption, and save.

Ptolemy takes the resulting `.cdr` file, as well as `.dmg` and `.iso` images.

## What has been tried

On a 13.6-inch MacBook Air with the M4 (10-core CPU and GPU, 24 GB), macOS 27, with Tiger 10.4.11 and Leopard 10.5 on
the G4 7447A. Leopard, Dual G4, Photo Booth, Cinebench, Geekbench and Xbench were tried with this release; Classic and
SimCity 4 with beta 3, Halo with beta 1, Doom 3 with a build of the same day as beta 1, the others with builds of
the weeks before.

| Software | Result |
|---|---|
| Mac OS X Tiger 10.4 | Installs from the retail DVD image; updates to 10.4.11 with the Combo update; Finder and Dock with Quartz Extreme |
| Mac OS X Leopard 10.5 | Installs from the DVD image; desktop with Quartz Extreme; System Preferences, Chess, Software Update |
| Halo (Universal) | Campaign plays. Built-in time demo at 800 × 600 with 2× FSAA and lens flares: 87–95 fps (two runs) |
| Doom 3 1.3.1 | Renders completely (ARB2 path). `timedemo demo1` at 640 × 480: 55 fps |
| Half-Life (Xash3D) | 1024 × 768: 430–650 fps, depending on the run |
| SimCity 4 | Starts from its disc image; tutorial cities render with their trees and buildings |
| Fallout 2 | 640 × 480 in 256 colours |
| Classic (Mac OS 9.2.2) | Starts in Tiger; Age of Empires plays |
| Cinebench R10 | Tiger: CPU 3138, OpenGL about 5200. Leopard: CPU 3141, OpenGL 5696. Leopard on Dual G4: x CPU 5461 (1.76×) |
| Geekbench 2.2 | Tiger 5084, Leopard 4943, Leopard on Dual G4 6067 (a real dual 1.42 GHz Power Mac G4 averages about 1220) |
| Xbench 1.3 | Tiger: User Interface 294, OpenGL 212, vecLib FFT 66 |
| Photo Booth (Leopard) | Sees the Mac's camera as a USB camera |

Everything else is untested. Reports are welcome.

## Classic Environment

You can download a Mac OS 9.2.2 System Folder for Mac OS X Tiger from the Internet (for example, the “Classic folder” archive from Macintosh Repository) and install it:

1. Unpack the archive on your Mac with The Unarchiver or StuffIt Expander. They keep the files’ resource forks, which Mac OS 9 needs.
2. In Finder, Control-click the unpacked folder and choose Compress. Finder’s zip keeps the resource forks.
3. In Ptolemy, choose Media › Insert Disk Image… and select the zip. Tiger shows it as a disk with the zip’s name.
4. In Tiger, drag the System Folder from that disk onto Tiger’s disk.
5. Open System Preferences › Classic, select that System Folder and click Start.

Don’t drag Mac OS 9 files onto Ptolemy’s window: that copy loses their resource forks.

## Known limitations

- **Graphics.** Every case of the OpenGL comparison test matches Apple's software renderer, but some details are
  approximate: edge flags in wireframe mode (every edge is drawn), the R300's 3- and 6-sample FSAA modes (drawn with
  4 samples) and its exact sample positions. See [docs/graphics.md](docs/graphics.md).
- **Sleep.** Mac OS X cannot sleep in Ptolemy: Tiger stops, Leopard hangs. In Tiger, set Energy Saver to never put
  the computer to sleep; in Leopard, Ptolemy Tools keep the system from sleeping on its own.
- **NVRAM.** Mac OS X's NVRAM is emulated as the Core99 flash it expects. The first start of a machine made with an
  earlier beta converts its NVRAM file once and keeps the original next to it as `nvram.bin.legacy`. Ptolemy chooses
  the startup disk itself, so Startup Disk has no effect.
- **Classic.** Classic (Mac OS 9 inside Tiger) needs a Mac OS 9.2.2 System Folder that you bring into Tiger yourself,
  in a disk image or a StuffIt archive (files dropped one by one lose their resource forks); Ptolemy does not include
  one. The first start takes about two minutes. As on real Macs, Classic has no 3D. Mac OS 9 itself does not start:
  the firmware cannot start a disk that Tiger marked for Mac OS 9.
- **Files.** Files dropped onto the window arrive without resource forks, type and creator. One file can be up to 4 GB.
- **Benchmarks.** Most code runs much faster than on a real G4, but some floating-point library code is slower: in Leopard,
  Xbench's vecLib FFT scores about 63, where a 1.33 GHz iBook G4 scores about 71.
- **Dual G4.** Leopard only (Tiger wakes every processor 100 times a second, which costs a second processor its idle
  time). Threads that hand one lock back and forth are much slower than on one processor, and software that shares
  memory without the PowerPC's barriers can misbehave more often than on a real dual G4.
- **Not available:** two CPUs in Tiger, a G5, CD audio, game controllers, sound input, incoming network
  connections.
- **Mouse in games.** When a game switches the display mode, Ptolemy captures the mouse: Ctrl+Option or Cmd+Tab
  releases it, a click captures it again. A captured mouse can feel fast; lower the game's own mouse sensitivity.

## Plans

Dual G4 runs in Leopard. A Power Mac G5 is postponed indefinitely.

## Building

You need the Xcode command line tools and Python 3.

```sh
sh build.sh --test        # the engine (build/ptolemy-engine) and its tests
```

The firmware ROM, Ptolemy Tools and the app have their own steps: see [docs/building.md](docs/building.md).

## Documentation

- [docs/architecture.md](docs/architecture.md): how the pieces fit together
- [docs/building.md](docs/building.md): building the engine, the ROM, Ptolemy Tools and Ptolemy.app
- [docs/metal.md](docs/metal.md): how Ptolemy uses Metal
- [docs/graphics.md](docs/graphics.md): what the Radeon model covers and what it does not cover yet
- [epicycle/README.md](epicycle/README.md): the CPU engine and its tests

## License

GPL-2.0-or-later. See [LICENSE](LICENSE). Mac OS X, Metal, Halo, Doom 3 and the other names above are trademarks of
their owners; Ptolemy is not affiliated with Apple or with any of them.
